// SPDX-License-Identifier: GPL-3.0-or-later
// Helpers specific to the game's engine (see runtime/hle/fusion/).
#pragma once

#include <string>

namespace recomp::hle {

// Writes the game's encrypted scripts and configs, decrypted and decompressed by
// its own loaders, below `dir` with their asset names (Lua 5.1 bytecode with 4-byte
// numbers, JSON, zip). Needs the engine set up (after nativeConfig). Returns the
// number of files, or -1 if it cannot run.
int dump_encrypted_assets(const std::string& dir);

}  // namespace recomp::hle
