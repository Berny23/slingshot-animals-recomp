// SPDX-License-Identifier: GPL-3.0-or-later
#include "elf_image.hpp"

#include <cstring>
#include <fstream>
#include <iterator>

namespace recomp {
namespace {

constexpr uint32_t PT_LOAD = 1;
constexpr uint32_t PF_X = 1;
constexpr uint32_t SHT_DYNSYM = 11;
constexpr uint32_t SHT_REL = 9;

template <typename T>
T rd(const std::vector<uint8_t>& f, size_t off) {
    T v;
    std::memcpy(&v, f.data() + off, sizeof(T));
    return v;
}

uint32_t arm_expand_imm(uint32_t insn) {
    uint32_t imm8 = insn & 0xff, rot = ((insn >> 8) & 0xf) * 2;
    return rot ? (imm8 >> rot) | (imm8 << (32 - rot)) : imm8;
}

}  // namespace

std::optional<ElfImage> ElfImage::load(const std::string& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open " + path;
        return std::nullopt;
    }
    ElfImage img;
    img.file_.assign(std::istreambuf_iterator<char>(in), {});
    const auto& f = img.file_;
    if (f.size() < 52 || std::memcmp(f.data(), "\x7f" "ELF", 4) != 0 || f[4] != 1 || f[5] != 1) {
        error = "not a little-endian ELF32 file";
        return std::nullopt;
    }
    if (rd<uint16_t>(f, 18) != 40) {
        error = "not an ARM ELF";
        return std::nullopt;
    }

    const uint32_t phoff = rd<uint32_t>(f, 28), shoff = rd<uint32_t>(f, 32);
    const uint16_t phentsize = rd<uint16_t>(f, 42), phnum = rd<uint16_t>(f, 44);
    const uint16_t shentsize = rd<uint16_t>(f, 46), shnum = rd<uint16_t>(f, 48);
    const uint16_t shstrndx = rd<uint16_t>(f, 50);

    uint32_t end = 0;
    for (uint16_t i = 0; i < phnum; ++i) {
        size_t o = phoff + size_t(i) * phentsize;
        Segment s;
        s.type = rd<uint32_t>(f, o);
        s.offset = rd<uint32_t>(f, o + 4);
        s.vaddr = rd<uint32_t>(f, o + 8);
        s.filesz = rd<uint32_t>(f, o + 16);
        s.memsz = rd<uint32_t>(f, o + 20);
        s.flags = rd<uint32_t>(f, o + 24);
        img.segments_.push_back(s);
        if (s.type == PT_LOAD) end = std::max(end, s.vaddr + s.memsz);
    }
    img.memory_.assign(end, 0);
    for (const auto& s : img.segments_) {
        if (s.type != PT_LOAD) continue;
        if (size_t(s.offset) + s.filesz > f.size()) {
            error = "segment outside file";
            return std::nullopt;
        }
        std::memcpy(img.memory_.data() + s.vaddr, f.data() + s.offset, s.filesz);
    }

    const size_t shstr_off = rd<uint32_t>(f, shoff + size_t(shstrndx) * shentsize + 16);
    for (uint16_t i = 0; i < shnum; ++i) {
        size_t o = shoff + size_t(i) * shentsize;
        Section s;
        s.name = reinterpret_cast<const char*>(f.data() + shstr_off + rd<uint32_t>(f, o));
        s.type = rd<uint32_t>(f, o + 4);
        s.flags = rd<uint32_t>(f, o + 8);
        s.addr = rd<uint32_t>(f, o + 12);
        s.offset = rd<uint32_t>(f, o + 16);
        s.size = rd<uint32_t>(f, o + 20);
        img.sections_.push_back(s);
    }

    for (size_t si = 0; si < img.sections_.size(); ++si) {
        const auto& s = img.sections_[si];
        if (s.type != SHT_DYNSYM) continue;
        size_t o = shoff + si * shentsize;
        const auto& strtab = img.sections_[rd<uint32_t>(f, o + 24)];  // sh_link
        for (uint32_t p = s.offset; p + 16 <= s.offset + s.size; p += 16) {
            Symbol sym;
            sym.name = reinterpret_cast<const char*>(f.data() + strtab.offset + rd<uint32_t>(f, p));
            sym.value = rd<uint32_t>(f, p + 4);
            sym.size = rd<uint32_t>(f, p + 8);
            uint8_t info = f[p + 12];
            sym.type = info & 0xf;
            sym.bind = info >> 4;
            sym.shndx = rd<uint16_t>(f, p + 14);
            img.dynsyms_.push_back(std::move(sym));
        }
    }
    for (const auto& s : img.sections_) {
        if (s.type != SHT_REL) continue;
        for (uint32_t p = s.offset; p + 8 <= s.offset + s.size; p += 8) {
            uint32_t info = rd<uint32_t>(f, p + 4);
            img.relocs_.push_back({rd<uint32_t>(f, p), info & 0xff, info >> 8});
        }
    }
    if (const Section* t = img.section(".text")) {
        img.text_begin_ = t->addr;
        img.text_end_ = t->addr + t->size;
    }
    img.build_plt_map();
    return img;
}

const Section* ElfImage::section(const std::string& name) const {
    for (const auto& s : sections_)
        if (s.name == name) return &s;
    return nullptr;
}

bool ElfImage::is_exec(uint32_t addr) const {
    for (const auto& s : segments_)
        if (s.type == PT_LOAD && (s.flags & PF_X) && addr >= s.vaddr && addr < s.vaddr + s.memsz)
            return true;
    return false;
}

// PLT stubs (ARM mode, 12 bytes each, after a 20-byte header):
//   add ip, pc, #imm_a   ; ip = stub + 8 + imm_a
//   add ip, ip, #imm_b
//   ldr pc, [ip, #imm12]!
void ElfImage::build_plt_map() {
    const Section* plt = section(".plt");
    if (!plt) return;
    std::unordered_map<uint32_t, std::string> got_to_name;
    for (const auto& r : relocs_)
        if (r.type == R_ARM_JUMP_SLOT && r.sym < dynsyms_.size())
            got_to_name[r.offset] = dynsyms_[r.sym].name;

    for (uint32_t a = plt->addr + 20; a + 12 <= plt->addr + plt->size; a += 12) {
        uint32_t i0 = read32(a), i1 = read32(a + 4), i2 = read32(a + 8);
        if ((i0 & 0x0ffff000) != 0x028fc000 || (i1 & 0x0ffff000) != 0x028cc000 ||
            (i2 & 0x0ffff000) != 0x05bcf000)
            continue;
        uint32_t got = a + 8 + arm_expand_imm(i0) + arm_expand_imm(i1) + (i2 & 0xfff);
        if (auto it = got_to_name.find(got); it != got_to_name.end()) plt_imports_[a] = it->second;
    }
}

}  // namespace recomp
