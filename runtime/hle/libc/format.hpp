// SPDX-License-Identifier: GPL-3.0-or-later
// printf/scanf engines operating on guest arguments.
#pragma once

#include <cstdint>
#include <string>

#include "recomp/hle.hpp"

namespace recomp::hle {

// Formats `fmt` (a host pointer into guest memory) pulling arguments from `args`.
std::string guest_format(const char* fmt, ArgReader& args);

// sscanf-style parsing into guest pointers. Returns the C return value.
int guest_scan(const char* input, const char* fmt, ArgReader& args);

}  // namespace recomp::hle
