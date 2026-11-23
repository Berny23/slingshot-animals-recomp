// SPDX-License-Identifier: GPL-3.0-or-later
#include "cheats.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "recomp/vfs.hpp"

namespace slingshot::mods {
namespace {

bool g_cheated = false;

std::filesystem::path marker() { return std::filesystem::path(recomp::vfs_root()) / "cheated"; }

}  // namespace

void mark_save_cheated() {
    if (g_cheated) return;
    g_cheated = true;
    std::error_code ec;
    if (std::filesystem::exists(marker(), ec)) return;
    std::ofstream(marker()) << "This save was played with a cheat mod.\n";
    std::fprintf(stderr, "mods: save marked as cheated, no Recomp Achievements for it\n");
}

bool save_cheated() {
    if (!g_cheated) {
        std::error_code ec;
        g_cheated = std::filesystem::exists(marker(), ec);
    }
    return g_cheated;
}

}  // namespace slingshot::mods
