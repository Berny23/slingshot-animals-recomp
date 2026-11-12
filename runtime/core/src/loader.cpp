// SPDX-License-Identifier: GPL-3.0-or-later
// Loads the original ELF image into guest memory at its link address (base 0),
// applies relocations and resolves imports against the HLE registry.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

#include "recomp/generated.hpp"
#include "recomp/memory.hpp"
#include "recomp/runtime.hpp"
#include "runtime_internal.hpp"

namespace recomp {
namespace {

constexpr uint32_t PT_LOAD = 1, PT_DYNAMIC = 2;
constexpr uint32_t DT_NULL = 0, DT_HASH = 4, DT_STRTAB = 5, DT_SYMTAB = 6, DT_REL = 17, DT_RELSZ = 18,
                   DT_JMPREL = 23, DT_PLTRELSZ = 2, DT_INIT = 12, DT_INIT_ARRAY = 25, DT_INIT_ARRAYSZ = 27;
constexpr uint32_t R_ARM_ABS32 = 2, R_ARM_GLOB_DAT = 21, R_ARM_JUMP_SLOT = 22, R_ARM_RELATIVE = 23;

struct Dynsym {
    std::string name;
    uint32_t value;
    uint8_t type, bind;
    uint16_t shndx;
};

std::vector<Dynsym> g_syms;
std::unordered_map<std::string, uint32_t> g_defined;  // name -> value (with Thumb bit)
uint32_t g_init = 0, g_init_array = 0, g_init_array_sz = 0;
uint32_t g_phdr = 0;
uint16_t g_phnum = 0;

template <typename T>
T rd(const std::vector<uint8_t>& f, size_t off) {
    T v;
    std::memcpy(&v, f.data() + off, sizeof(T));
    return v;
}

// Unresolved import: report once, return 0.
void missing_import(CPU* c, const char* name) {
    static std::unordered_map<std::string, int> seen;
    if (!seen[name]++) std::fprintf(stderr, "recomp: unimplemented import %s (lr=0x%08x)\n", name, c->r[14]);
    c->r[0] = 0;
}

}  // namespace

uint32_t lib_program_headers(uint16_t& count) {
    count = g_phnum;
    return g_phdr;
}

uint32_t lib_symbol(const char* name) {
    auto it = g_defined.find(name);
    return it == g_defined.end() ? 0 : it->second;
}

bool runtime_init(const std::string& lib_path) {
    if (!mem::init()) return false;
    hle::run_init_hooks();

    std::ifstream in(lib_path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "recomp: cannot open %s\n", lib_path.c_str());
        return false;
    }
    const std::vector<uint8_t> f((std::istreambuf_iterator<char>(in)), {});
    if (f.size() < 52 || std::memcmp(f.data(), "\x7f" "ELF", 4) != 0 || rd<uint16_t>(f, 18) != 40) {
        std::fprintf(stderr, "recomp: %s is not an ARM ELF file\n", lib_path.c_str());
        return false;
    }
    if (!internal::verify_library(f)) return false;

    // Segments.
    const uint32_t phoff = rd<uint32_t>(f, 28);
    const uint16_t phentsize = rd<uint16_t>(f, 42), phnum = rd<uint16_t>(f, 44);
    uint32_t dyn = 0;
    // The headers live in the unmapped first page, so keep a copy for dl_iterate_phdr.
    g_phnum = phnum;
    g_phdr = mem::static_alloc(uint32_t(phnum) * 32, 4);
    for (uint16_t i = 0; i < phnum; ++i) std::memcpy(g_mem + g_phdr + i * 32u, f.data() + phoff + size_t(i) * phentsize, 32);
    for (uint16_t i = 0; i < phnum; ++i) {
        const size_t o = phoff + size_t(i) * phentsize;
        const uint32_t type = rd<uint32_t>(f, o), off = rd<uint32_t>(f, o + 4), vaddr = rd<uint32_t>(f, o + 8);
        const uint32_t filesz = rd<uint32_t>(f, o + 16), memsz = rd<uint32_t>(f, o + 20);
        if (type == PT_DYNAMIC) dyn = vaddr;
        if (type != PT_LOAD) continue;
        // The first page (ELF header) stays unmapped so that null pointers fault.
        const uint32_t skip = vaddr < mem::kNullPageEnd ? mem::kNullPageEnd - vaddr : 0;
        if (filesz > skip) std::memcpy(g_mem + vaddr + skip, f.data() + off + skip, filesz - skip);
        if (memsz > filesz) std::memset(g_mem + vaddr + filesz, 0, memsz - filesz);
        internal::note_segment(vaddr, memsz);
    }
    if (!dyn) {
        std::fprintf(stderr, "recomp: no dynamic section\n");
        return false;
    }

    // Dynamic table (read from the loaded image).
    uint32_t hash = 0, strtab = 0, symtab = 0, rel = 0, relsz = 0, jmprel = 0, pltrelsz = 0;
    for (uint32_t a = dyn;; a += 8) {
        const uint32_t tag = ld32(a), val = ld32(a + 4);
        if (tag == DT_NULL) break;
        switch (tag) {
            case DT_HASH: hash = val; break;
            case DT_STRTAB: strtab = val; break;
            case DT_SYMTAB: symtab = val; break;
            case DT_REL: rel = val; break;
            case DT_RELSZ: relsz = val; break;
            case DT_JMPREL: jmprel = val; break;
            case DT_PLTRELSZ: pltrelsz = val; break;
            case DT_INIT: g_init = val; break;
            case DT_INIT_ARRAY: g_init_array = val; break;
            case DT_INIT_ARRAYSZ: g_init_array_sz = val; break;
        }
    }
    // The symbol table may live in the (unmapped) first page: read it from the file.
    const uint32_t nsyms = rd<uint32_t>(f, hash + 4);
    for (uint32_t i = 0; i < nsyms; ++i) {
        const size_t p = symtab + size_t(i) * 16;
        Dynsym s;
        s.name = reinterpret_cast<const char*>(f.data() + strtab + rd<uint32_t>(f, p));
        s.value = rd<uint32_t>(f, p + 4);
        s.type = f[p + 12] & 0xf;
        s.bind = f[p + 12] >> 4;
        s.shndx = rd<uint16_t>(f, p + 14);
        if (s.shndx != 0) g_defined.emplace(s.name, s.value);
        g_syms.push_back(std::move(s));
    }

    // Import resolution for the generated import table.
    internal::resolve_generated_imports(missing_import);

    auto resolve = [&](uint32_t symidx, bool& ok) -> uint32_t {
        ok = true;
        const Dynsym& s = g_syms.at(symidx);
        if (s.shndx != 0) return s.value;
        uint32_t data;
        if (hle::find_data(s.name.c_str(), data)) return data;
        if (s.type == 1 /*STT_OBJECT*/) {
            std::fprintf(stderr, "recomp: unresolved data import %s\n", s.name.c_str());
            ok = false;
            return 0;
        }
        if (hle::find_import(s.name.c_str()) || s.bind != 2 /*STB_WEAK*/) return internal::import_thunk(s.name.c_str());
        return 0;  // weak, unimplemented: leave null so "if (fn)" checks work
    };
    size_t bad = 0;
    auto apply = [&](uint32_t start, uint32_t size) {
        for (uint32_t a = start; a < start + size; a += 8) {
            const uint32_t off = ld32(a), info = ld32(a + 4), type = info & 0xff, sym = info >> 8;
            bool ok = true;
            switch (type) {
                case R_ARM_RELATIVE: break;  // loaded at base 0: nothing to do
                case R_ARM_ABS32: st32(off, ld32(off) + resolve(sym, ok)); break;
                case R_ARM_GLOB_DAT:
                case R_ARM_JUMP_SLOT: st32(off, resolve(sym, ok)); break;
                default:
                    std::fprintf(stderr, "recomp: unsupported relocation type %u at 0x%x\n", type, off);
                    ok = false;
            }
            bad += !ok;
        }
    };
    apply(rel, relsz);
    apply(jmprel, pltrelsz);
    if (bad) std::fprintf(stderr, "recomp: %zu relocations could not be resolved\n", bad);

    internal::init_main_thread();
    return true;
}

void run_constructors() {
    CPU* c = current_cpu();
    if (g_init) guest_call(c, g_init);
    for (uint32_t i = 0; i < g_init_array_sz / 4; ++i) {
        const uint32_t fn = ld32(g_init_array + 4 * i);
        if (fn && fn != 0xffffffffu) guest_call(c, fn);
    }
}

const char* function_name(uint32_t addr) {
    static std::unordered_map<uint32_t, const char*> by_addr = [] {
        std::unordered_map<uint32_t, const char*> m;
        for (const auto& s : g_syms)
            if (s.shndx != 0 && s.type == 2) m.emplace(s.value & ~1u, s.name.c_str());
        return m;
    }();
    auto it = by_addr.find(addr & ~1u);
    return it == by_addr.end() ? nullptr : it->second;
}

}  // namespace recomp
