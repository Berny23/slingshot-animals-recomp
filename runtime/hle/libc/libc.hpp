// SPDX-License-Identifier: GPL-3.0-or-later
// Shared helpers of the libc HLE modules.
#pragma once

#include <cstdint>
#include <string>

#include "format.hpp"
#include "recomp/hle.hpp"

namespace recomp::hle {

// Per-thread guest errno (returned by __errno()).
uint32_t guest_errno_addr();
void set_guest_errno(int e);  // a host errno value (stored as Linux's)

// Guest address of the thunk for an import (for dlsym and function pointers).
uint32_t internal_import_thunk(const char* name);

// Maps a guest file system path to a host path (see vfs.cpp).
std::string host_path(const char* guest_path);

}  // namespace recomp::hle
