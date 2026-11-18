// SPDX-License-Identifier: GPL-3.0-or-later
// What the core needs from the host operating system (os.cpp: POSIX and Windows).
#pragma once

#include <pthread.h>

#include <cstddef>
#include <cstdint>

namespace recomp::os {

// Reserves `size` bytes of address space of which [begin, end) may be used
// after commit(); the rest always faults.
uint8_t* reserve_arena(size_t size, size_t begin, size_t end);
// Makes part of [begin, end) usable. Windows commits memory here; elsewhere the
// whole range is usable from the start and the OS commits pages on first use.
bool commit(void* p, size_t size);
// Gives the memory back; the range reads as zeros after the next commit().
void discard(void* p, size_t size);

// Fatal faults (segfault, illegal instruction, ...) call `report` with a
// description and the faulting address, then end the process.
void install_fault_handler(void (*report)(const char* what, uintptr_t addr));
// Signal-based report of another thread (POSIX): `report` runs on that thread.
void install_thread_report(void (*report)());
void print_backtrace();

#ifdef _WIN32
// Windows has no signals to interrupt a thread: it is suspended instead.
uint32_t thread_id(pthread_t thread);
uint32_t current_thread_id();
// Stops `thread`, calls `stopped(arg)`, takes up to `max` host return addresses
// and lets the thread go on. Returns the number of addresses.
int suspended_backtrace(pthread_t thread, void (*stopped)(void*), void* arg, void** frames, int max);
void print_frames(void* const* frames, int n);  // as module+offset
#endif

}  // namespace recomp::os
