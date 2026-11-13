// SPDX-License-Identifier: GPL-3.0-or-later
// Locale, dynamic linker iteration and raw system calls.
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "libc.hpp"
#include "recomp/game.hpp"
#include "recomp/memory.hpp"
#include "recomp/runtime.hpp"

namespace recomp {
namespace hle {
namespace {

using u32 = uint32_t;

u32 h_setlocale(int, u32) {
    static u32 c = mem::static_string("C");
    return c;
}

// dl_iterate_phdr: reports the game library only.
int h_dl_iterate_phdr(u32 callback, u32 data) {
    uint16_t count = 0;
    const u32 phdr = lib_program_headers(count);
    static u32 info = 0, name = 0;
    if (!info) {
        info = mem::static_alloc(16, 4);
        name = mem::static_string(std::string("/data/app-lib/") + game.package + "/lib" + game.library + ".so");
    }
    st32(info, 0);  // dlpi_addr: loaded at its link address
    st32(info + 4, name);
    st32(info + 8, phdr);
    st16(info + 12, count);
    return int32_t(guest_call(callback, {info, 16, data}));
}

// The unwinder's lookup of the exception index table (PT_ARM_EXIDX); libdl's
// dl_unwind_find_exidx is the same lookup.
u32 h___gnu_Unwind_Find_exidx(u32, u32 count_out) {
    uint16_t n = 0;
    const u32 ph = lib_program_headers(n);
    for (u32 i = 0; i < n; ++i) {
        const u32 e = ph + 32 * i;
        if (ld32(e) == 0x70000001) {  // PT_ARM_EXIDX
            st32(count_out, ld32(e + 20) / 8);
            return ld32(e + 8);
        }
    }
    st32(count_out, 0);
    return 0;
}

int h_raise(int sig) {
    std::fprintf(stderr, "recomp: guest raised signal %d\n", sig);
    if (sig == SIGABRT || sig == SIGSEGV || sig == SIGILL || sig == SIGBUS || sig == SIGFPE) std::abort();
    return 0;
}

// setjmp/longjmp (and sigsetjmp/siglongjmp). The jmp_buf (bionic: 64 words) holds the callee-saved
// registers r4-r11, sp, lr and d8-d15. longjmp resumes the setjmp caller at
// the return address through a GuestUnwind, like a C++ exception.
int h_setjmp(CPU* c) {
    const u32 b = c->r[0];
    for (int i = 4; i <= 11; ++i) st32(b + 4 * u32(i - 4), c->r[i]);
    st32(b + 32, c->r[13]);
    st32(b + 36, c->r[14]);
    for (int i = 0; i < 8; ++i) st64(b + 40 + 8 * u32(i), c->d[8 + i]);
    return 0;
}
[[noreturn]] void h_longjmp(CPU* c) {
    const u32 b = c->r[0];
    const u32 value = c->r[1] ? c->r[1] : 1;
    for (int i = 4; i <= 11; ++i) c->r[i] = ld32(b + 4 * u32(i - 4));
    c->r[13] = ld32(b + 32);
    c->r[14] = ld32(b + 36);
    for (int i = 0; i < 8; ++i) c->d[8 + i] = ld64(b + 40 + 8 * u32(i));
    c->r[0] = value;
    throw GuestUnwind{c->r[14] & ~1u, c->r[13]};
}

// Linux ARM EABI system call numbers used by the game.
u32 do_syscall(u32 nr, ArgReader& a) {
    switch (nr) {
        case 20: return u32(::getpid());                       // getpid
        case 224: return u32(::syscall(SYS_gettid));           // gettid
        case 4: {                                              // write
            const int fd = int(a.u32());
            const u32 buf = a.u32(), n = a.u32();
            return fd == 1 || fd == 2 ? u32(::write(fd, g_mem + buf, n)) : u32(-EBADF);
        }
        case 158: return u32(::sched_yield());                 // sched_yield
        case 263: {                                            // clock_gettime
            const int id = int(a.u32());
            const u32 ts = a.u32();
            timespec t{};
            ::clock_gettime(clockid_t(id), &t);
            st32(ts, u32(t.tv_sec));
            st32(ts + 4, u32(t.tv_nsec));
            return 0;
        }
        default:
            if (trace_enabled("syscalls")) std::fprintf(stderr, "recomp: unsupported syscall %u\n", nr);
            return u32(-ENOSYS);
    }
}

// syscall(nr, ...): returns -1 and sets errno like the libc wrapper.
void h_syscall(CPU* c) {
    ArgReader a(c);
    const u32 nr = a.u32();
    const int32_t r = int32_t(do_syscall(nr, a));
    if (r < 0 && r > -4096) {
        set_guest_errno(-r);
        c->r[0] = u32(-1);
    } else {
        c->r[0] = u32(r);
    }
}

RECOMP_REGISTER_IMPORT("setlocale", RECOMP_HLE(h_setlocale));
RECOMP_REGISTER_IMPORT("dl_iterate_phdr", RECOMP_HLE(h_dl_iterate_phdr));
RECOMP_REGISTER_IMPORT("syscall", h_syscall);
RECOMP_REGISTER_IMPORT("__gnu_Unwind_Find_exidx", RECOMP_HLE(h___gnu_Unwind_Find_exidx));
RECOMP_REGISTER_IMPORT("dl_unwind_find_exidx", RECOMP_HLE(h___gnu_Unwind_Find_exidx));
RECOMP_REGISTER_IMPORT("raise", RECOMP_HLE(h_raise));
RECOMP_REGISTER_IMPORT("setjmp", RECOMP_HLE(h_setjmp));
RECOMP_REGISTER_IMPORT("longjmp", h_longjmp);
RECOMP_REGISTER_IMPORT("_setjmp", RECOMP_HLE(h_setjmp));
RECOMP_REGISTER_IMPORT("_longjmp", h_longjmp);
// The signal mask is not saved: the game blocks no signals.
RECOMP_REGISTER_IMPORT("sigsetjmp", RECOMP_HLE(h_setjmp));
RECOMP_REGISTER_IMPORT("siglongjmp", h_longjmp);

}  // namespace
}  // namespace hle

// SVC #0 from generated code: raw kernel interface (returns -errno).
void svc(CPU* c, uint32_t) {
    // Arguments are in r0-r6, the number in r7.
    hle::ArgReader a(c);
    c->r[0] = hle::do_syscall(c->r[7], a);
}

}  // namespace recomp
