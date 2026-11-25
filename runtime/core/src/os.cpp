// SPDX-License-Identifier: GPL-3.0-or-later
#include "os.hpp"

#include <cstdio>
#include <cstdlib>
#include <initializer_list>

#ifdef _WIN32
#include <windows.h>
#elif defined(__SWITCH__)
#include <switch.h>

#include <cstring>
#include <mutex>
#include <vector>
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

void print_frames(void* const* frames, int n) {
    for (int i = 0; i < n; ++i) {
        HMODULE module = nullptr;
        char path[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<LPCSTR>(frames[i]), &module))
            GetModuleFileNameA(module, path, sizeof path);
        const char* name = path;
        for (const char* p = path; *p; ++p)
            if (*p == '\\' || *p == '/') name = p + 1;
        std::fprintf(stderr, "  %s+0x%zx\n", name,
                     size_t(static_cast<const char*>(frames[i]) - reinterpret_cast<const char*>(module)));
    }
}

void print_backtrace() {
    void* frames[64];
    print_frames(frames, CaptureStackBackTrace(0, 64, frames, nullptr));
}

uint32_t thread_id(pthread_t thread) { return GetThreadId(reinterpret_cast<HANDLE>(pthread_gethandle(thread))); }
uint32_t current_thread_id() { return GetCurrentThreadId(); }

int suspended_backtrace(pthread_t thread, void (*stopped)(void*), void* arg, void** frames, int max) {
    const HANDLE h = reinterpret_cast<HANDLE>(pthread_gethandle(thread));
    if (!h || SuspendThread(h) == DWORD(-1)) return 0;
    CONTEXT ctx{};
    ctx.ContextFlags = CONTEXT_FULL;
    int n = 0;
    if (GetThreadContext(h, &ctx)) {
        stopped(arg);
#if defined(__x86_64__)
        DWORD64& pc = ctx.Rip;
#else
        DWORD64& pc = ctx.Pc;
#endif
        while (n < max && pc) {
            frames[n++] = reinterpret_cast<void*>(pc);
            DWORD64 base = 0;
            PRUNTIME_FUNCTION f = RtlLookupFunctionEntry(pc, &base, nullptr);
            if (!f) {  // leaf function: the return address is on top of the stack (x64) or in lr
#if defined(__x86_64__)
                pc = *reinterpret_cast<DWORD64*>(ctx.Rsp);
                ctx.Rsp += 8;
#else
                if (pc == ctx.Lr) break;
                pc = ctx.Lr;
#endif
                continue;
            }
            void* handler_data = nullptr;
            DWORD64 frame = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, pc, f, &ctx, &handler_data, &frame, nullptr);
        }
    }
    ResumeThread(h);
    return n;
}

#elif defined(__SWITCH__)

// Horizon maps memory only on request. Usually the heap holds all of the process's
// memory (the homebrew loader sets it up so): then 2 MiB chunks of the heap are
// mapped into a reservation in the ASLR region as code memory (read/write) and
// stay mapped until exit. Where enough memory is left outside the heap and the
// process may use svcMapPhysicalMemory, the arena lies in the alias region and
// pages are backed as they are committed (and given back on discard).
namespace {
constexpr size_t kPage = 0x1000;
constexpr size_t kChunk = 0x200000;
constexpr uint64_t kPhysicalMinimum = 256ull << 20;  // free memory outside the heap for svcMapPhysicalMemory
uint8_t* g_arena = nullptr;
size_t g_arena_size = 0;
bool g_physical = false;
std::mutex g_map_lock;
std::vector<bool> g_mapped;  // per page (physical) or per chunk (code memory)

bool map_physical_pages(size_t first, size_t count) {
    // Runs of unmapped pages, one call each.
    for (size_t i = first; i < first + count;) {
        if (g_mapped[i]) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < first + count && !g_mapped[j]) ++j;
        if (R_FAILED(svcMapPhysicalMemory(g_arena + i * kPage, (j - i) * kPage))) return false;
        std::memset(g_arena + i * kPage, 0, (j - i) * kPage);
        for (size_t k = i; k < j; ++k) g_mapped[k] = true;
        i = j;
    }
    return true;
}

bool map_code_chunk(size_t chunk) {
    void* src = aligned_alloc(kPage, kChunk);
    if (!src) return false;
    std::memset(src, 0, kChunk);
    Handle h = INVALID_HANDLE;
    if (R_FAILED(svcCreateCodeMemory(&h, src, kChunk)) ||
        R_FAILED(svcControlCodeMemory(h, CodeMapOperation_MapOwner, g_arena + chunk * kChunk, kChunk, Perm_Rw))) {
        if (h != INVALID_HANDLE) svcCloseHandle(h);
        std::free(src);
        return false;
    }
    g_mapped[chunk] = true;  // the handle and the source stay until exit
    return true;
}
}  // namespace

uint8_t* reserve_arena(size_t size, size_t, size_t) {
    size = (size + kChunk - 1) & ~(kChunk - 1);
    u64 alias = 0, alias_size = 0;
    svcGetInfo(&alias, InfoType_AliasRegionAddress, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&alias_size, InfoType_AliasRegionSize, CUR_PROCESS_HANDLE, 0);
    u64 total = 0, used = 0;
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    const uint64_t free_memory = total > used ? total - used : 0;
    const uint64_t base = (alias + kChunk - 1) & ~uint64_t(kChunk - 1);
    Result probe = MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    if (alias_size >= size + (base - alias) && free_memory >= kPhysicalMinimum) {
        probe = svcMapPhysicalMemory(reinterpret_cast<void*>(base), kPage);
        if (R_SUCCEEDED(probe)) svcUnmapPhysicalMemory(reinterpret_cast<void*>(base), kPage);
    }
    std::fprintf(stderr, "recomp: memory %llu MiB, used %llu MiB, svcMapPhysicalMemory 0x%x\n",
                 static_cast<unsigned long long>(total >> 20), static_cast<unsigned long long>(used >> 20), probe);
    if (R_SUCCEEDED(probe)) {
        g_arena = reinterpret_cast<uint8_t*>(base);
        g_physical = true;
        g_mapped.assign(size / kPage, false);
    } else if (!envIsSyscallHinted(0x21) ||  // svcSendSyncRequest: no hints from the loader at all
               (envIsSyscallHinted(0x4b) && envIsSyscallHinted(0x4c))) {  // svcCreateCodeMemory, svcControlCodeMemory
        virtmemLock();
        g_arena = static_cast<uint8_t*>(virtmemFindAslr(size, kChunk));
        if (g_arena) virtmemAddReservation(g_arena, size);
        virtmemUnlock();
        g_mapped.assign(size / kChunk, false);
    }
    g_arena_size = size;
    if (!g_arena) std::fprintf(stderr, "recomp: cannot reserve the guest address space\n");
    else std::fprintf(stderr, "recomp: guest memory by %s\n", g_physical ? "svcMapPhysicalMemory" : "code memory");
    return g_arena;
}

bool commit(void* p, size_t size) {
    std::lock_guard lk(g_map_lock);
    const size_t begin = size_t(static_cast<uint8_t*>(p) - g_arena), end = begin + size;
    bool ok = true;
    if (g_physical) {
        ok = map_physical_pages(begin / kPage, (end + kPage - 1) / kPage - begin / kPage);
    } else {
        for (size_t c = begin / kChunk; ok && c < (end + kChunk - 1) / kChunk; ++c)
            if (!g_mapped[c]) ok = map_code_chunk(c);
    }
    if (!ok) std::fprintf(stderr, "recomp: out of memory (committing %zu bytes)\n", size);
    return ok;
}

void discard(void* p, size_t size) {
    std::lock_guard lk(g_map_lock);
    const size_t begin = size_t(static_cast<uint8_t*>(p) - g_arena), end = begin + size;
    if (!g_physical) {
        std::memset(p, 0, size);  // read as zeros, as after a new commit
        return;
    }
    // Whole pages only; a partly covered page may still hold live guest data.
    const size_t first = (begin + kPage - 1) / kPage, last = end / kPage;
    for (size_t i = first; i < last;) {
        if (!g_mapped[i]) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < last && g_mapped[j]) ++j;
        svcUnmapPhysicalMemory(g_arena + i * kPage, (j - i) * kPage);
        for (size_t k = i; k < j; ++k) g_mapped[k] = false;
        i = j;
    }
}

namespace {
const char* exception_name(ThreadExceptionDesc d) {
    switch (d) {
        case ThreadExceptionDesc_InstructionAbort: return "instruction abort";
        case ThreadExceptionDesc_MisalignedPC: return "misaligned pc";
        case ThreadExceptionDesc_MisalignedSP: return "misaligned sp";
        case ThreadExceptionDesc_SError: return "serror";
        case ThreadExceptionDesc_BadSVC: return "bad svc";
        case ThreadExceptionDesc_Trap: return "illegal instruction";
        default: return "segmentation fault";
    }
}
}  // namespace

void install_fault_handler(void (*report)(const char*, uintptr_t)) { g_report = report; }
void install_thread_report(void (*)()) {}
void print_backtrace() {}

}  // namespace recomp::os

// libnx calls this on a fatal CPU exception (with its own stack below).
extern "C" {
alignas(16) uint8_t __nx_exception_stack[0x8000];
uint64_t __nx_exception_stack_size = sizeof(__nx_exception_stack);
void __libnx_exception_handler(ThreadExceptionDump* ctx) {
    std::fprintf(stderr, "recomp: crash at pc 0x%llx\n", static_cast<unsigned long long>(ctx->pc.x));
    if (recomp::os::g_report) recomp::os::g_report(recomp::os::exception_name(ThreadExceptionDesc(ctx->error_desc)), uintptr_t(ctx->far.x));
    std::fflush(stderr);
}
}

namespace recomp::os {

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
