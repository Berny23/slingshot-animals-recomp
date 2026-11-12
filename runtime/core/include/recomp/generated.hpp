// SPDX-License-Identifier: GPL-3.0-or-later
// Interface between the runtime and the generated code (tables emitted by abrecomp).
#pragma once

#include <cstddef>
#include <cstdint>

#include "recomp/cpu.hpp"

namespace recomp::gen {

struct FuncEntry {
    uint32_t addr;  // guest entry address, bit 0 set for Thumb functions
    GuestFunc fn;
};

extern const char* const lib_sha256;      // library the code was generated from
extern const FuncEntry functions[];       // sorted by address
extern const size_t function_count;
// Original code of the functions wrapped by a runtime hook (abrecomp emit --wrap).
struct OriginalEntry {
    const char* hook;
    GuestFunc fn;
};
extern const OriginalEntry originals[];
extern const size_t original_count;
extern const char* const imports[];       // indexed by recomp::call_import's index
extern const size_t import_count;

}  // namespace recomp::gen
