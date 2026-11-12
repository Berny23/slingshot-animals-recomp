// SPDX-License-Identifier: GPL-3.0-or-later
// Private interfaces between the runtime core's translation units.
#pragma once

#include <cstdint>
#include <vector>

#include "recomp/cpu.hpp"

namespace recomp::internal {

bool verify_library(const std::vector<uint8_t>& file);
void note_segment(uint32_t vaddr, uint32_t memsz);
// Fills the generated import table; unknown names call `missing(c, name)`.
void resolve_generated_imports(void (*missing)(CPU*, const char*));
uint32_t import_thunk(const char* name);
void init_main_thread();
void init_kuser_page();
void install_crash_handler();

}  // namespace recomp::internal
