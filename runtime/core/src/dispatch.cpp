// SPDX-License-Identifier: GPL-3.0-or-later
// Function dispatch, import binding, thunks, guest threads and CPU support
// routines called from generated code.
#include <pthread.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>

#include "recomp/generated.hpp"
#include "recomp/memory.hpp"
#include "os.hpp"
#include "recomp/runtime.hpp"
#include "runtime_internal.hpp"
#include "sha256.hpp"

namespace recomp {
namespace {

struct Thunk {
    hle::HostFn fn;
    std::string name;
};

std::mutex g_thunk_lock;
// Never destroyed: game threads may still call imports while the process exits.
std::vector<Thunk>& thunks() {
    static auto* v = new std::vector<Thunk>;
    return *v;
}
std::mutex g_import_lock;
std::unordered_map<std::string, uint32_t>& g_import_thunks = *new std::unordered_map<std::string, uint32_t>;

std::unordered_map<std::string, hle::HostFn>& import_registry() {
    static std::unordered_map<std::string, hle::HostFn> m;
    return m;
}
std::unordered_map<std::string, uint32_t>& data_registry() {
    static std::unordered_map<std::string, uint32_t> m;
    return m;
}

std::vector<hle::HostFn> g_import_fns;  // indexed like gen::imports
void (*g_missing)(CPU*, const char*) = nullptr;

thread_local CPU* t_cpu = nullptr;

}  // namespace

// ------------------------------------------------------------------ registry
namespace hle {
void register_import(const char* name, HostFn fn) { import_registry()[name] = fn; }
void register_data(const char* name, uint32_t addr) { data_registry()[name] = addr; }
HostFn find_import(const char* name) {
    auto it = import_registry().find(name);
    return it == import_registry().end() ? nullptr : it->second;
}
std::vector<void (*)()>& init_hooks() {
    static std::vector<void (*)()> v;
    return v;
}
void register_init_hook(void (*fn)()) { init_hooks().push_back(fn); }
void run_init_hooks() {
    for (auto fn : init_hooks()) fn();
}
bool find_data(const char* name, uint32_t& addr) {
    auto it = data_registry().find(name);
    if (it == data_registry().end()) return false;
    addr = it->second;
    return true;
}
}  // namespace hle

uint32_t make_thunk(hle::HostFn fn, const char* name) {
    std::lock_guard lk(g_thunk_lock);
    auto& t = thunks();
    if (t.size() >= (mem::kThunkEnd - mem::kThunkBase) / 4) {
        std::fprintf(stderr, "recomp: too many thunks\n");
        std::abort();
    }
    t.push_back({fn, name});
    return mem::kThunkBase + 4 * uint32_t(t.size() - 1);
}

const char* thunk_name(uint32_t addr) {
    if (addr < mem::kThunkBase || addr >= mem::kThunkEnd) return nullptr;
    std::lock_guard lk(g_thunk_lock);
    const uint32_t i = (addr - mem::kThunkBase) / 4;
    return i < thunks().size() ? thunks()[i].name.c_str() : nullptr;
}

namespace internal {

uint32_t import_thunk(const char* name) {
    std::lock_guard lk(g_import_lock);  // dlsym calls this from any thread
    auto it = g_import_thunks.find(name);
    if (it != g_import_thunks.end()) return it->second;
    const uint32_t a = make_thunk(hle::find_import(name), name);
    g_import_thunks[name] = a;
    return a;
}

void resolve_generated_imports(void (*missing)(CPU*, const char*)) {
    g_missing = missing;
    g_import_fns.resize(gen::import_count);
    size_t unresolved = 0;
    for (size_t i = 0; i < gen::import_count; ++i) {
        g_import_fns[i] = hle::find_import(gen::imports[i]);
        // A call through the PLT to a function the library exports itself.
        if (!g_import_fns[i])
            if (const uint32_t own = lib_symbol(gen::imports[i])) g_import_fns[i] = lookup_function(own);
        unresolved += g_import_fns[i] == nullptr;
    }
    if (trace_enabled("imports"))
        for (size_t i = 0; i < gen::import_count; ++i)
            if (!g_import_fns[i]) std::fprintf(stderr, "recomp: no HLE for import %s\n", gen::imports[i]);
    std::fprintf(stderr, "recomp: %zu/%zu imports implemented\n", gen::import_count - unresolved, gen::import_count);
}

bool verify_library(const std::vector<uint8_t>& file) {
    const std::string digest = sha256_hex(file.data(), file.size());
    if (gen::lib_sha256[0] && digest != gen::lib_sha256) {
        std::fprintf(stderr,
                     "recomp: library mismatch: the code was generated from sha256 %s,\n"
                     "        but the supplied library has sha256 %s\n",
                     gen::lib_sha256, digest.c_str());
        return false;
    }
    return true;
}

// The first loadable segment holds the code (and the PLT).
uint32_t g_code_begin = 0, g_code_end = 0;
void note_segment(uint32_t vaddr, uint32_t memsz) {
    if (g_code_end) return;
    g_code_begin = std::max(vaddr, mem::kNullPageEnd);
    g_code_end = vaddr + memsz;
}

void init_main_thread() {
    init_kuser_page();
    install_crash_handler();
    CPU* c = create_cpu(8u << 20);
    bind_cpu(c);
}

}  // namespace internal

// ------------------------------------------------------------------ kernel user helpers
// Old ARM code calls these fixed addresses in the vector page directly
// (bionic's atomics, libgcc's __sync_* fallbacks).
namespace {
void kuser_cmpxchg64(CPU* c) {  // r0 = &old, r1 = &new, r2 = ptr; r0 = 0 and C set on success
    uint64_t expected = ld64(c->r[0]);
    const bool ok = __atomic_compare_exchange_n(reinterpret_cast<uint64_t*>(g_mem + c->r[2]), &expected,
                                                ld64(c->r[1]), false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    c->r[0] = ok ? 0 : 1;
    c->c = ok;
}
void kuser_memory_barrier(CPU*) { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
void kuser_cmpxchg(CPU* c) {  // r0 = old, r1 = new, r2 = ptr
    uint32_t expected = c->r[0];
    const bool ok = __atomic_compare_exchange_n(reinterpret_cast<uint32_t*>(g_mem + c->r[2]), &expected, c->r[1],
                                                false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    c->r[0] = ok ? 0 : 1;
    c->c = ok;
}
void kuser_get_tls(CPU* c) { c->r[0] = c->tls; }

GuestFunc kuser_helper(uint32_t addr) {
    switch (addr) {
        case 0xffff0f60: return kuser_cmpxchg64;
        case 0xffff0fa0: return kuser_memory_barrier;
        case 0xffff0fc0: return kuser_cmpxchg;
        case 0xffff0fe0: return kuser_get_tls;
        default: return nullptr;
    }
}
}  // namespace

namespace internal {
void init_kuser_page() { st32(0xffff0ffc, 5); }  // __kuser_helper_version
}

// ------------------------------------------------------------------ lookup / calls
GuestFunc lookup_function(uint32_t addr) {
    if (addr >= mem::kKuserBase) return kuser_helper(addr);
    if (addr >= mem::kThunkBase) {
        std::lock_guard lk(g_thunk_lock);
        const uint32_t i = (addr - mem::kThunkBase) / 4;
        if (i < thunks().size()) {
            if (thunks()[i].fn) return thunks()[i].fn;
            return nullptr;
        }
        return nullptr;
    }
    const uint32_t a = addr & ~1u;
    const gen::FuncEntry* b = gen::functions;
    const gen::FuncEntry* e = gen::functions + gen::function_count;
    const gen::FuncEntry* it = std::lower_bound(b, e, a, [](const gen::FuncEntry& x, uint32_t v) { return (x.addr & ~1u) < v; });
    if (it != e && (it->addr & ~1u) == a) return it->fn;
    // An ARM PLT stub (exception tables point at the personality routine's):
    //   add ip, pc, #a ; add ip, ip, #b ; ldr pc, [ip, #c]!  -> the function in its GOT slot
    if (!(addr & 3) && a >= internal::g_code_begin && a + 12 <= internal::g_code_end && (ld32(a) & 0xfffff000u) == 0xe28fc000u && (ld32(a + 4) & 0xfffff000u) == 0xe28cc000u &&
        (ld32(a + 8) & 0xfffff000u) == 0xe5bcf000u) {
        auto imm = [](uint32_t insn) {  // ARM modified immediate
            const uint32_t v = insn & 0xff, rot = 2 * ((insn >> 8) & 0xf);
            return rot ? (v >> rot) | (v << (32 - rot)) : v;
        };
        const uint32_t slot = a + 8 + imm(ld32(a)) + imm(ld32(a + 4)) + (ld32(a + 8) & 0xfff);
        const uint32_t target = ld32(slot);
        if (target && (target & ~1u) != a) return lookup_function(target);
    }
    return nullptr;
}

void call_indirect(CPU* c, uint32_t target) {
    if (GuestFunc f = lookup_function(target)) {
        f(c);
        return;
    }
    if (const char* n = thunk_name(target)) {
        g_missing(c, n);
        return;
    }
    char buf[96];
    std::snprintf(buf, sizeof buf, "call to unknown guest address 0x%08x", target);
    trap(c, target, buf);
}

void jump_indirect(CPU* c, uint32_t target) {
    // A computed jump that leaves the function: equivalent to a tail call.
    call_indirect(c, target);
}

void call_import(CPU* c, uint32_t index) {
    if (hle::HostFn f = g_import_fns[index]) {
        f(c);
        return;
    }
    g_missing(c, gen::imports[index]);
}

uint32_t guest_call(CPU* c, uint32_t addr, std::initializer_list<uint32_t> args) {
    // Preserve the caller-visible state so host code can call into the guest re-entrantly.
    const uint32_t saved_sp = c->r[13], saved_lr = c->r[14];
    uint32_t saved[4];
    std::memcpy(saved, c->r, sizeof saved);
    unsigned i = 0;
    const size_t stack_args = args.size() > 4 ? args.size() - 4 : 0;
    uint32_t sp = (c->r[13] - uint32_t(4 * stack_args)) & ~7u;
    c->r[13] = sp;
    for (uint32_t v : args) {
        if (i < 4) c->r[i] = v;
        else st32(sp + 4 * (i - 4), v);
        ++i;
    }
    c->r[14] = mem::kThunkBase - 4;  // sentinel return address (never dispatched)
    call_indirect(c, addr);
    const uint32_t r0 = c->r[0];
    c->r[13] = saved_sp;
    c->r[14] = saved_lr;
    // r0/r1 keep the results; r2/r3 are restored for the benefit of host callers.
    c->r[2] = saved[2];
    c->r[3] = saved[3];
    return r0;
}

// ------------------------------------------------------------------ threads / CPUs
CPU* current_cpu() { return t_cpu; }
void bind_cpu(CPU* c) { t_cpu = c; }

static std::mutex g_stack_lock;
static std::unordered_map<CPU*, uint32_t> g_stacks;  // guest stack of each CPU

CPU* create_cpu(uint32_t stack_size) {
    CPU* c = static_cast<CPU*>(::operator new(sizeof(CPU), std::align_val_t(64)));
    std::memset(c, 0, sizeof(CPU));
    stack_size = (stack_size + 4095) & ~4095u;
    const uint32_t stack = mem::alloc(stack_size, 4096);
    if (!stack) {
        std::fprintf(stderr, "recomp: out of guest memory for a %u byte stack\n", stack_size);
        std::abort();
    }
    c->r[13] = stack + stack_size - 64;
    // Bionic-style TLS block: slot 0 points to itself.
    const uint32_t tls = mem::alloc_zeroed(64 * 4);
    st32(tls, tls);
    c->tls = tls;
    std::lock_guard lk(g_stack_lock);
    g_stacks[c] = stack;
    return c;
}

void destroy_cpu(CPU* c) {
    {
        std::lock_guard lk(g_stack_lock);
        const auto it = g_stacks.find(c);
        if (it != g_stacks.end()) {
            mem::free(it->second);
            g_stacks.erase(it);
        }
    }
    mem::free(c->tls);
    ::operator delete(c, std::align_val_t(64));
}

// ------------------------------------------------------------------ CPU support for generated code
static std::mutex g_excl_lock;

uint32_t ldrex32(CPU* c, uint32_t a) {
    const uint32_t v = __atomic_load_n(reinterpret_cast<uint32_t*>(g_mem + a), __ATOMIC_SEQ_CST);
    c->excl_addr = a;
    c->excl_value = v;
    c->excl_valid = 1;
    return v;
}
uint32_t ldrex16(CPU* c, uint32_t a) {
    const uint16_t v = __atomic_load_n(reinterpret_cast<uint16_t*>(g_mem + a), __ATOMIC_SEQ_CST);
    c->excl_addr = a;
    c->excl_value = v;
    c->excl_valid = 2;
    return v;
}
uint32_t ldrex8(CPU* c, uint32_t a) {
    const uint8_t v = __atomic_load_n(g_mem + a, __ATOMIC_SEQ_CST);
    c->excl_addr = a;
    c->excl_value = v;
    c->excl_valid = 3;
    return v;
}
uint64_t ldrexd(CPU* c, uint32_t a) {
    const uint64_t v = __atomic_load_n(reinterpret_cast<uint64_t*>(g_mem + a), __ATOMIC_SEQ_CST);
    c->excl_addr = a;
    c->excl_value = v;
    c->excl_valid = 4;
    return v;
}
// Store-exclusive succeeds if memory still holds the value seen by the load
// (compare-and-swap emulation of the exclusive monitor).
template <typename T>
static uint32_t strex_impl(CPU* c, uint32_t a, T v, uint8_t kind) {
    if (c->excl_valid != kind || c->excl_addr != a) return 1;
    c->excl_valid = 0;
    T expected = T(c->excl_value);
    return __atomic_compare_exchange_n(reinterpret_cast<T*>(g_mem + a), &expected, v, false, __ATOMIC_SEQ_CST,
                                       __ATOMIC_SEQ_CST)
               ? 0
               : 1;
}
uint32_t strex32(CPU* c, uint32_t a, uint32_t v) { return strex_impl<uint32_t>(c, a, v, 1); }
uint32_t strex16(CPU* c, uint32_t a, uint32_t v) { return strex_impl<uint16_t>(c, a, uint16_t(v), 2); }
uint32_t strex8(CPU* c, uint32_t a, uint32_t v) { return strex_impl<uint8_t>(c, a, uint8_t(v), 3); }
uint32_t strexd(CPU* c, uint32_t a, uint64_t v) { return strex_impl<uint64_t>(c, a, v, 4); }
void clrex(CPU* c) { c->excl_valid = 0; }
void barrier() { std::atomic_thread_fence(std::memory_order_seq_cst); }

void mrc(CPU* c, uint32_t pc, uint32_t rt, uint32_t enc) {
    std::fprintf(stderr, "recomp: unsupported mrc 0x%08x at 0x%08x\n", enc, pc);
    if (rt < 15) c->r[rt] = 0;
}

void unimplemented(CPU* c, uint32_t pc, const char* text) {
    static std::mutex lock;
    static std::unordered_map<uint32_t, int> seen;
    std::lock_guard lk(lock);
    if (!seen[pc]++) std::fprintf(stderr, "recomp: unimplemented instruction at 0x%08x: %s\n", pc, text);
    (void)c;
    (void)g_excl_lock;
}

void dump_cpu(const CPU* c) {
    for (int i = 0; i < 16; i += 4)
        std::fprintf(stderr, "  r%-2d %08x  r%-2d %08x  r%-2d %08x  r%-2d %08x\n", i, c->r[i], i + 1, c->r[i + 1], i + 2,
                     c->r[i + 2], i + 3, c->r[i + 3]);
    std::fprintf(stderr, "  nzcv %d%d%d%d\n", c->n, c->z, c->c, c->v);
    // Guest return addresses found on the stack (heuristic backtrace).
    std::fprintf(stderr, "  stack:");
    int shown = 0;
    for (uint32_t a = c->r[13]; a < c->r[13] + 4096 && shown < 16; a += 4) {
        const uint32_t v = ld32(a);
        if (lookup_function(v) || (v > 0x1000 && v < 0x00a00000 && (v & 1))) {
            std::fprintf(stderr, " %08x", v);
            ++shown;
        }
    }
    std::fprintf(stderr, "\n");
}

// Fatal faults: report the guest address and the host backtrace, whose
// fn_XXXXXXXX frames name the guest functions involved.
namespace {
void fault_report(const char* what, uintptr_t a) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(g_mem);
    if (a >= base && a < base + (uintptr_t(1) << 32))
        std::fprintf(stderr, "recomp: %s accessing guest address 0x%08x\n", what, uint32_t(a - base));
    else
        std::fprintf(stderr, "recomp: %s at host address 0x%zx\n", what, size_t(a));
    if (CPU* c = current_cpu()) dump_cpu(c);
    os::print_backtrace();
}
// request_thread_report: the same report for a thread that is still running,
// e.g. one that hangs; it continues afterwards.
void thread_report() {
    std::fprintf(stderr, "recomp: thread report\n");
    if (CPU* c = current_cpu()) dump_cpu(c);
    os::print_backtrace();
}
}  // namespace

void request_thread_report(pthread_t thread) {
#if defined(_WIN32) || defined(__SWITCH__)
    (void)thread;
    std::fprintf(stderr, "recomp: thread reports need POSIX signals\n");
#else
    pthread_kill(thread, SIGUSR1);
#endif
}

namespace internal {
void install_crash_handler() {
    os::install_fault_handler(fault_report);
    os::install_thread_report(thread_report);
}
}  // namespace internal

void trap(CPU* c, uint32_t pc, const char* what) {
    const char* name = function_name(pc);
    std::fprintf(stderr, "recomp: trap at 0x%08x%s%s: %s\n", pc, name ? " in " : "", name ? name : "", what);
    dump_cpu(c);
    std::abort();
}

bool trace_enabled(const char* channel) {
    static const char* env = std::getenv("RECOMP_TRACE");
    if (!env) return false;
    const std::string s = std::string(",") + env + ",";
    return s.find(std::string(",") + channel + ",") != std::string::npos || s.find(",all,") != std::string::npos;
}

}  // namespace recomp
