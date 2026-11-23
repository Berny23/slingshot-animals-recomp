// SPDX-License-Identifier: GPL-3.0-or-later
// Shared mod helper: saves played with a cheat mod. Such a save has a file
// "cheated" in its folder (recomp::vfs_root()), which stays when the mod is turned
// off; Recomp Achievements gets no achievements from it.
#pragma once

namespace slingshot::mods {

// Call once the save folder is known (on_frame, not the mod's constructor).
void mark_save_cheated();
bool save_cheated();

}  // namespace slingshot::mods
