// SPDX-License-Identifier: GPL-3.0-or-later
// Android packages: the base APK (manifest and resources, from aapt2) with the
// code, native libraries and the game files added, aligned for apksigner.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace apk {

struct PackInput {
    std::filesystem::path base;  // from aapt2 link
    std::filesystem::path dex;   // classes.dex
    std::string abi;             // arm64-v8a, x86_64, ...
    std::vector<std::filesystem::path> libs;  // lib/<abi>/
    std::filesystem::path game;  // extracted game: lib/, assets/ and icon.png become assets/game/
};

// Writes the unsigned APK, with assets/game/files.txt listing the game files.
bool pack(const PackInput& in, const std::filesystem::path& out, std::string& error);

}  // namespace apk
