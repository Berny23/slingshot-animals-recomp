// SPDX-License-Identifier: GPL-3.0-or-later
// Guest address space: a 4 GiB host reservation with a fixed layout, plus a
// thread-safe heap that hands out guest addresses.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "recomp/cpu.hpp"

namespace recomp::mem {

// Fixed guest layout.
constexpr uint32_t kNullPageEnd = 0x00001000;  // [0, 4K) is never mapped: null dereferences fault
constexpr uint32_t kHleDataBase = 0x01000000;  // static data provided by the runtime (ctype tables, __sF, ...)
constexpr uint32_t kHleDataSize = 0x00f00000;
constexpr uint32_t kHeapBase = 0x02000000;
constexpr uint32_t kHeapEnd = 0xfff00000;
constexpr uint32_t kThunkBase = 0xfff00000;    // import thunk addresses (never executed, only dispatched)
constexpr uint32_t kThunkEnd = 0xffff0000;
constexpr uint32_t kKuserBase = 0xffff0000;    // Linux ARM kernel user helpers (__kuser_cmpxchg, ...)

bool init();  // reserve the address space

// Guest heap (malloc semantics; 8-byte alignment). Returns 0 on failure.
uint32_t alloc(uint32_t size, uint32_t align = 8);
uint32_t alloc_zeroed(uint32_t size);
uint32_t realloc(uint32_t addr, uint32_t size);
void free(uint32_t addr);
uint32_t usable_size(uint32_t addr);

// Bump allocator for permanent runtime data in the HLE data area.
uint32_t static_alloc(uint32_t size, uint32_t align = 8);

// Helpers.
inline char* str(uint32_t a) { return a ? reinterpret_cast<char*>(g_mem + a) : nullptr; }
inline void* ptr(uint32_t a) { return a ? static_cast<void*>(g_mem + a) : nullptr; }
template <typename T>
inline T* as(uint32_t a) { return a ? reinterpret_cast<T*>(g_mem + a) : nullptr; }
// Guest address of a host pointer that points into guest memory.
inline uint32_t guest(const void* p) {
    return p ? uint32_t(static_cast<const uint8_t*>(p) - g_mem) : 0;
}
bool is_guest(const void* p);
// Copy a host string into new guest heap memory.
uint32_t dup_string(std::string_view s);
uint32_t static_string(std::string_view s);

}  // namespace recomp::mem
