// SPDX-License-Identifier: GPL-3.0-or-later
// The Nintendo Switch executable before elf2nro.
#pragma once

#include <string>

namespace apk {

// Copies an AArch64 ELF and removes the Guarded Control Stack code that newer
// GCCs link in (libgcc's unwinder). It only runs where CHKFEAT reports GCS,
// which the Switch's Cortex-A57 never does, but Ryujinx's JIT refuses to
// translate a block holding `mrs GCSPR_EL0` and takes the emulator down on the
// first C++ exception. `mrs Xt, GCSPR_EL0` and `gcspopm Xt` become
// `mov Xt, #0` (`nop` for xzr). Returns the number of changed instructions,
// or -1 with `error`.
int patch_switch_elf(const std::string& in, const std::string& out, std::string& error);

}  // namespace apk
