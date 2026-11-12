// SPDX-License-Identifier: GPL-3.0-or-later
// <stdlib.h> memory/process functions, errno, system information, dynamic
// linker queries, signals and the network (always offline).
#include <sched.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <random>
#include <string>
#include <vector>

#include "libc.hpp"
#include "recomp/hle.hpp"
#include "recomp/memory.hpp"
#include "recomp/runtime.hpp"

namespace recomp::hle {

uint32_t guest_errno_addr() {
    static thread_local uint32_t addr = 0;
    if (!addr) addr = mem::alloc_zeroed(4);
    return addr;
}
void set_guest_errno(int e) { st32(guest_errno_addr(), uint32_t(e)); }

namespace {
using u32 = uint32_t;

// ---- heap
u32 h_malloc(u32 n) { return mem::alloc(n); }
u32 h_calloc(u32 n, u32 s) {
    const uint64_t t = uint64_t(n) * s;
    return t > 0xffffffffu ? 0 : mem::alloc_zeroed(u32(t));
}
u32 h_realloc(u32 p, u32 n) { return mem::realloc(p, n); }
void h_free(u32 p) { mem::free(p); }
u32 h_memalign(u32 align, u32 n) { return mem::alloc(n, std::max<u32>(align, 8)); }
int h_posix_memalign(u32 out, u32 align, u32 n) {
    const u32 a = mem::alloc(n, std::max<u32>(align, 8));
    if (!a) return ENOMEM;
    st32(out, a);
    return 0;
}
u32 h_malloc_usable_size(u32 p) { return mem::usable_size(p); }

// ---- errno
u32 h___errno() { return guest_errno_addr(); }

// ---- sorting with guest comparators
thread_local u32 t_cmp = 0;
int cmp_tramp(const void* a, const void* b) {
    return int32_t(guest_call(t_cmp, {mem::guest(a), mem::guest(b)}));
}
void h_qsort(u32 base, u32 n, u32 size, u32 cmp) {
    const u32 saved = t_cmp;
    t_cmp = cmp;
    std::qsort(g_mem + base, n, size, cmp_tramp);
    t_cmp = saved;
}
u32 h_bsearch(u32 key, u32 base, u32 n, u32 size, u32 cmp) {
    u32 lo = 0, hi = n;
    while (lo < hi) {
        const u32 mid = lo + (hi - lo) / 2, elem = base + mid * size;
        const int r = int32_t(guest_call(cmp, {key, elem}));
        if (r == 0) return elem;
        if (r < 0) hi = mid;
        else lo = mid + 1;
    }
    return 0;
}

// ---- random numbers (deterministic per run unless seeded)
std::mutex g_rand_lock;
uint64_t g_rand48 = 0x1234abcd330eull;
u32 h_lrand48() {
    std::lock_guard lk(g_rand_lock);
    g_rand48 = (g_rand48 * 0x5deece66dull + 0xb) & 0xffffffffffffull;
    return u32(g_rand48 >> 17);
}
void h_srand48(u32 seed) {
    std::lock_guard lk(g_rand_lock);
    g_rand48 = (uint64_t(seed) << 16) | 0x330e;
}

// ---- environment / process
u32 h_getenv(u32 name) {
    // The game sees a clean Android-like environment, not the host's.
    static std::mutex lock;
    static std::vector<std::pair<std::string, u32>> env;
    std::lock_guard lk(lock);
    if (env.empty()) {
        for (const char* kv : {"OPENSSL_armcap=0", "ANDROID_ROOT=/system", "ANDROID_DATA=/data", "HOME=/data"}) {
            const char* eq = std::strchr(kv, '=');
            env.emplace_back(std::string(kv, eq), mem::static_string(eq + 1));
        }
    }
    for (auto& [k, v] : env)
        if (k == mem::str(name)) return v;
    return 0;
}
void h_abort() {
    std::fprintf(stderr, "recomp: guest called abort()\n");
    dump_cpu(current_cpu());
    std::abort();
}
void h_exit(int code) {
    std::fprintf(stderr, "recomp: guest called exit(%d)\n", code);
    std::fflush(nullptr);
    std::_Exit(code);
}
void h___stack_chk_fail() {
    std::fprintf(stderr, "recomp: stack protector failure\n");
    dump_cpu(current_cpu());
    std::abort();
}

// atexit-style registrations: run at process exit (rarely matters for a game).
struct AtExit {
    u32 fn, arg, dso;
};
std::mutex g_atexit_lock;
std::vector<AtExit> g_atexit;
int h___cxa_atexit(u32 fn, u32 arg, u32 dso) {
    std::lock_guard lk(g_atexit_lock);
    g_atexit.push_back({fn, arg, dso});
    return 0;
}
void h___cxa_finalize(u32) {}

// ---- system information
u32 h_sysconf(int name) {
    switch (name) {
        case 0x27: /*_SC_PAGESIZE*/ case 0x28: /*_SC_PAGE_SIZE*/ return 4096;
        case 0x60: /*_SC_NPROCESSORS_CONF*/ case 0x61: /*_SC_NPROCESSORS_ONLN*/ return 4;
        case 0x06: /*_SC_CLK_TCK*/ return 100;
        case 0x62: /*_SC_PHYS_PAGES*/ return (1u << 30) / 4096;
        default: return u32(-1);
    }
}
int h_uname(u32 buf) {
    // struct utsname on bionic: 6 fields of 65 chars.
    const char* fields[6] = {"Linux", "localhost", "3.10.0", "#1 SMP", "armv7l", "localdomain"};
    for (int i = 0; i < 6; ++i) std::snprintf(mem::str(buf + 65 * i), 65, "%s", fields[i]);
    return 0;
}
int h_getpid() { return int(::getpid()); }
int h_geteuid() { return 10123; }  // an app uid
u32 h_getpwuid(u32) { return 0; }
int h_gethostname(u32 buf, u32 n) {
    std::snprintf(mem::str(buf), n, "localhost");
    return 0;
}

// __system_property_get: answers what an ordinary ARMv7 phone would say.
int h___system_property_get(u32 name, u32 value) {
    static const std::pair<const char*, const char*> props[] = {
        {"ro.build.version.sdk", "19"},       {"ro.build.version.release", "4.4.4"},
        {"ro.product.model", "Recomp"},       {"ro.product.manufacturer", "slingshot-animals-recomp"},
        {"ro.product.brand", "recomp"},       {"ro.product.device", "recomp"},
        {"ro.product.cpu.abi", "armeabi-v7a"}, {"ro.hardware", "recomp"},
        {"ro.build.fingerprint", "recomp/recomp/recomp:4.4.4/KTU84P/1:user/release-keys"},
        {"ro.serialno", "0123456789"},        {"ro.kernel.qemu", "0"},
        {"ro.debuggable", "0"},               {"ro.secure", "1"},
    };
    for (auto& [k, v] : props)
        if (std::strcmp(mem::str(name), k) == 0) {
            std::strcpy(mem::str(value), v);
            return int(std::strlen(v));
        }
    mem::str(value)[0] = 0;
    return 0;
}

// getauxval(AT_HWCAP): report no optional features, so OpenSSL and friends
// take their portable code paths.
u32 h_getauxval(u32) { return 0; }

// ---- dynamic linker
// Only the system libraries the game links against "exist"; their symbols
// are the HLE imports.
u32 h_dlopen(u32 name, int) {
    static u32 handle = mem::static_alloc(4, 4);
    if (trace_enabled("dl")) std::fprintf(stderr, "recomp: dlopen(%s)\n", name ? mem::str(name) : "null");
    return handle;
}
int h_dlclose(u32) { return 0; }
u32 h_dlerror() { return 0; }
u32 h_dlsym(u32, u32 name) {
    if (const u32 a = lib_symbol(mem::str(name))) return a;
    if (find_import(mem::str(name))) return internal_import_thunk(mem::str(name));
    if (trace_enabled("dl")) std::fprintf(stderr, "recomp: dlsym(%s) -> null\n", mem::str(name));
    return 0;
}
int h_dladdr(u32, u32) { return 0; }  // no symbol information

// ---- scheduling
int h_sched_yield() { return ::sched_yield(); }
int h_nanosleep(u32 req, u32 rem) {
    timespec ts{time_t(int32_t(ld32(req))), long(int32_t(ld32(req + 4)))};
    timespec r{};
    const int v = ::nanosleep(&ts, &r);
    if (rem) { st32(rem, u32(r.tv_sec)); st32(rem + 4, u32(r.tv_nsec)); }
    if (v) set_guest_errno(errno);
    return v;
}

// ---- signals: accepted and ignored (no asynchronous signals reach the guest).
int h_sigaction(int, u32, u32 old) {
    if (old) std::memset(g_mem + old, 0, 16);
    return 0;
}
int h_sigprocmask(int, u32, u32 old) {
    if (old) st32(old, 0);
    return 0;
}

// ---- networking: the online services are gone; every attempt fails cleanly.
int fail_net() {
    set_guest_errno(ENETUNREACH);
    return -1;
}
int h_socket(int, int, int) { set_guest_errno(EACCES); return -1; }
int h_connect(int, u32, u32) { return fail_net(); }
int h_bind(int, u32, u32) { return fail_net(); }
int h_send(int, u32, u32, int) { return fail_net(); }
int h_recv(int, u32, u32, int) { return fail_net(); }
int h_recvfrom(int, u32, u32, int, u32, u32) { return fail_net(); }
int h_getsockopt(int, int, int, u32, u32) { return fail_net(); }
int h_setsockopt(int, int, int, u32, u32) { return fail_net(); }
int h_getsockname(int, u32, u32) { return fail_net(); }
int h_getpeername(int, u32, u32) { return fail_net(); }
int h_getaddrinfo(u32, u32, u32, u32 res) {
    if (res) st32(res, 0);
    return -2;  // EAI_NONAME
}
void h_freeaddrinfo(u32) {}
u32 h_gai_strerror(int) { static u32 s = mem::static_string("network unavailable"); return s; }
u32 h_getservbyport(int, u32) { return 0; }
u32 h_inet_addr(u32 s) {
    unsigned a, b, c, d;
    if (std::sscanf(mem::str(s), "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return 0xffffffffu;
    return a | b << 8 | c << 16 | d << 24;
}
int h_inet_pton(int af, u32 src, u32 dst) {
    if (af != 2) return 0;
    const u32 v = h_inet_addr(src);
    if (v == 0xffffffffu) return 0;
    st32(dst, v);
    return 1;
}
u32 h_inet_ntop(int af, u32 src, u32 dst, u32 n) {
    if (af != 2) return 0;
    const uint8_t* b = g_mem + src;
    std::snprintf(mem::str(dst), n, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    return dst;
}
u32 h_if_nametoindex(u32) { return 0; }
u32 h_if_indextoname(u32, u32) { return 0; }

// ---- Android logging
int h___android_log_write(int prio, u32 tag, u32 text) {
    if (prio >= 3 || trace_enabled("log"))
        std::fprintf(stderr, "[%c/%s] %s\n", "??VDIWEFS"[prio & 7], mem::str(tag), mem::str(text));
    return 1;
}
void h___android_log_print(CPU* c) {
    ArgReader a(c);
    const int prio = int(a.u32());
    const u32 tag = a.u32(), fmt = a.u32();
    const std::string s = guest_format(mem::str(fmt), a);
    if (prio >= 3 || trace_enabled("log")) std::fprintf(stderr, "[%c/%s] %s\n", "??VDIWEFS"[prio & 7], mem::str(tag), s.c_str());
    c->r[0] = 1;
}

// ---- misc Google/bionic hooks
void h___google_potentially_blocking_region_begin() {}
void h___google_potentially_blocking_region_end() {}

#define REG(name) RECOMP_REGISTER_IMPORT(#name, RECOMP_HLE(h_##name))
REG(malloc); REG(calloc); REG(realloc); REG(free); REG(memalign); REG(posix_memalign); REG(malloc_usable_size);
REG(__errno); REG(qsort); REG(bsearch); REG(lrand48); REG(srand48); REG(getenv); REG(abort); REG(exit);
RECOMP_REGISTER_IMPORT("_exit", RECOMP_HLE(h_exit));
REG(__stack_chk_fail); REG(__cxa_atexit); REG(__cxa_finalize); REG(sysconf); REG(uname); REG(getpid); REG(geteuid);
REG(getpwuid); REG(gethostname); REG(__system_property_get); REG(getauxval); REG(dlsym); REG(dladdr); REG(dlopen); REG(dlclose); REG(dlerror); REG(sched_yield);
REG(nanosleep); REG(sigaction); REG(sigprocmask); REG(socket); REG(connect); REG(bind); REG(send); REG(recv);
REG(recvfrom); REG(getsockopt); REG(setsockopt); REG(getsockname); REG(getpeername); REG(getaddrinfo);
REG(freeaddrinfo); REG(gai_strerror); REG(getservbyport); REG(inet_addr); REG(inet_pton); REG(inet_ntop);
REG(if_nametoindex); REG(if_indextoname); REG(__android_log_write);
RECOMP_REGISTER_IMPORT("__android_log_print", h___android_log_print);
REG(__google_potentially_blocking_region_begin); REG(__google_potentially_blocking_region_end);

}  // namespace
}  // namespace recomp::hle
