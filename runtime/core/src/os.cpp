// SPDX-License-Identifier: GPL-3.0-or-later
#include "os.hpp"

#include <cstdio>
#include <cstdlib>
#include <initializer_list>

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <sys/mman.h>
#include <unistd.h>
#if __has_include(<execinfo.h>) && (!defined(__ANDROID__) || __ANDROID_API__ >= 33)
#include <execinfo.h>
#define RECOMP_HAVE_BACKTRACE 1
#endif
#endif

namespace recomp::os {
namespace {
void (*g_report)(const char*, uintptr_t) = nullptr;
}  // namespace

#ifdef _WIN32

uint8_t* reserve_arena(size_t size, size_t, size_t) {
    auto* p = static_cast<uint8_t*>(VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS));
    if (!p) std::fprintf(stderr, "recomp: cannot reserve the guest address space (error %lu)\n", GetLastError());
    return p;
}

bool commit(void* p, size_t size) {
    if (VirtualAlloc(p, size, MEM_COMMIT, PAGE_READWRITE)) return true;
    std::fprintf(stderr, "recomp: out of memory (committing %zu bytes, error %lu)\n", size, GetLastError());
    return false;
}

void discard(void* p, size_t size) { VirtualFree(p, size, MEM_DECOMMIT); }

namespace {
LONG CALLBACK fault_filter(EXCEPTION_POINTERS* e) {
    const DWORD code = e->ExceptionRecord->ExceptionCode;
    const char* what = nullptr;
    uintptr_t addr = reinterpret_cast<uintptr_t>(e->ExceptionRecord->ExceptionAddress);
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_IN_PAGE_ERROR:
            what = "access violation";
            addr = uintptr_t(e->ExceptionRecord->ExceptionInformation[1]);
            break;
        case EXCEPTION_ILLEGAL_INSTRUCTION: what = "illegal instruction"; break;
        case EXCEPTION_INT_DIVIDE_BY_ZERO: what = "division by zero"; break;
        case EXCEPTION_STACK_OVERFLOW: what = "stack overflow"; break;
        default: return EXCEPTION_CONTINUE_SEARCH;  // C++ exceptions, debugger events
    }
    if (g_report) g_report(what, addr);
    std::fflush(stderr);
    TerminateProcess(GetCurrentProcess(), 3);
    return EXCEPTION_CONTINUE_SEARCH;
}
}  // namespace

void install_fault_handler(void (*report)(const char*, uintptr_t)) {
    g_report = report;
    AddVectoredExceptionHandler(0, fault_filter);  // after any debugger's
}

void install_thread_report(void (*)()) {}  // no signals to interrupt another thread

void print_backtrace() {
    void* frames[64];
    const USHORT n = CaptureStackBackTrace(0, 64, frames, nullptr);
    for (USHORT i = 0; i < n; ++i) std::fprintf(stderr, "  %p\n", frames[i]);
}

#else

uint8_t* reserve_arena(size_t size, size_t begin, size_t end) {
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_NORESERVE
    flags |= MAP_NORESERVE;
#endif
    void* p = mmap(nullptr, size, PROT_NONE, flags, -1, 0);
    if (p == MAP_FAILED) {
        std::perror("recomp: reserving guest address space");
        return nullptr;
    }
    auto* m = static_cast<uint8_t*>(p);
    // Host pages larger than the guest's 4 KiB: the null guard shrinks (begin rounds
    // down) rather than hiding guest data.
    const size_t page = size_t(sysconf(_SC_PAGESIZE));
    begin &= ~(page - 1);
    end = (end + page - 1) & ~(page - 1);
    if (mprotect(m + begin, end - begin, PROT_READ | PROT_WRITE) != 0) {
        std::perror("recomp: mprotect");
        return nullptr;
    }
    return m;
}

bool commit(void*, size_t) { return true; }

void discard(void* p, size_t size) {
    // Whole host pages only (16 KiB on Apple silicon and some Android devices):
    // a partly covered page may still hold live guest data.
    static const uintptr_t page = uintptr_t(sysconf(_SC_PAGESIZE));
    const uintptr_t begin = (reinterpret_cast<uintptr_t>(p) + page - 1) & ~(page - 1);
    const uintptr_t end = (reinterpret_cast<uintptr_t>(p) + size) & ~(page - 1);
    if (end <= begin) return;
    p = reinterpret_cast<void*>(begin);
    size = end - begin;
#ifdef __APPLE__
    madvise(p, size, MADV_FREE);  // macOS has no zeroing MADV_DONTNEED
    mmap(p, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
#else
    madvise(p, size, MADV_DONTNEED);
#endif
}

namespace {
void (*g_thread_report)() = nullptr;

void fault_handler(int sig, siginfo_t* info, void*) {
    const char* what = sig == SIGSEGV ? "segmentation fault"
                       : sig == SIGBUS ? "bus error"
                       : sig == SIGILL ? "illegal instruction"
                                       : "arithmetic exception";
    if (g_report) g_report(what, reinterpret_cast<uintptr_t>(info->si_addr));
    signal(sig, SIG_DFL);
    raise(sig);
}
void report_handler(int, siginfo_t*, void*) {
    if (g_thread_report) g_thread_report();
}
}  // namespace

void install_fault_handler(void (*report)(const char*, uintptr_t)) {
    g_report = report;
    static char alt[64 * 1024];
    stack_t ss{};
    ss.ss_sp = alt;
    ss.ss_size = sizeof alt;
    sigaltstack(&ss, nullptr);
    struct sigaction sa{};
    sa.sa_sigaction = fault_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE}) sigaction(sig, &sa, nullptr);
}

void install_thread_report(void (*report)()) {
    g_thread_report = report;
    struct sigaction sa{};
    sa.sa_sigaction = report_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, nullptr);
}

void print_backtrace() {
#ifdef RECOMP_HAVE_BACKTRACE
    void* frames[64];
    const int n = backtrace(frames, 64);
    backtrace_symbols_fd(frames, n, 2);
#endif
}

#endif

}  // namespace recomp::os
