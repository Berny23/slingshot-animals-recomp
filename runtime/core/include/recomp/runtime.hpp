// SPDX-License-Identifier: GPL-3.0-or-later
// Runtime entry points used by the platform layer and the HLE modules.
#pragma once

#include <pthread.h>

#include <cstdint>
#include <initializer_list>
#include <string>

#include "recomp/cpu.hpp"
#include "recomp/hle.hpp"

namespace recomp {

// Loads the original library image (data, relocations) into guest memory and
// links it against the HLE imports. Must be called once before anything else.
bool runtime_init(const std::string& lib_path);
// Runs the library's DT_INIT / DT_INIT_ARRAY constructors on the calling thread.
void run_constructors();

// Address of a symbol defined by the game library (0 if absent).
uint32_t lib_symbol(const char* name);
// Guest copy of the library's program headers (for dl_iterate_phdr).
uint32_t lib_program_headers(uint16_t& count);

// Host function callable from guest code at the returned guest address
// (used for JNI function tables, callbacks, ...).
uint32_t make_thunk(hle::HostFn fn, const char* name);
// Name of the thunk/import at a guest address, or nullptr.
const char* thunk_name(uint32_t addr);

// Guest function lookup (bit 0 = Thumb, ignored). nullptr if unknown.
GuestFunc lookup_function(uint32_t addr);
const char* function_name(uint32_t addr);  // symbol name if known

// Per-thread guest CPU. The main thread's CPU is created by runtime_init.
CPU* current_cpu();
CPU* create_cpu(uint32_t stack_size);
void bind_cpu(CPU* c);   // make c the current thread's CPU
void destroy_cpu(CPU* c);

// Calls a guest function with up to N 32-bit arguments (extra ones go on the
// guest stack). Returns r0; r1 holds the high word of 64-bit results.
uint32_t guest_call(CPU* c, uint32_t addr, std::initializer_list<uint32_t> args = {});
inline uint32_t guest_call(uint32_t addr, std::initializer_list<uint32_t> args = {}) {
    return guest_call(current_cpu(), addr, args);
}

// Diagnostics.
void dump_cpu(const CPU* c);
bool trace_enabled(const char* channel);  // RECOMP_TRACE=imports,jni,... environment variable
// Prints the guest registers, guest stack and host backtrace of `thread` to
// stderr, which then carries on (for hangs). Not on the Switch.
void request_thread_report(pthread_t thread);

}  // namespace recomp
