// SPDX-License-Identifier: GPL-3.0-or-later
// Lua patches for the game's scripts (assets data/**/*.lua).
//
// A patched script is served as Lua source, which every game takes (see
// runtime/hle/android/asset_manager.cpp). It runs the original chunk, decrypted by
// the game's own code, in the environment the engine loads the script into, and
// then the mods' code. That code looks names up in that
// environment first and in _G after it, and what it defines goes into the environment.
//
//     slingshot::mods::append_lua("data/scripts/menus/main_menu.lua", R"(
//         local update = MainMenu.update
//         function MainMenu:update(...) ... end
//     )");
//
// The engine may load a script more than once (into a table of its own and into
// the globals); the code runs each time. Call append_lua from a mod's constructor:
// patches apply to scripts loaded afterwards.
#pragma once

#include <string>

namespace slingshot::mods {

// `script` is the asset name as the game opens it.
void append_lua(const std::string& script, const std::string& lua);

}  // namespace slingshot::mods
