// SPDX-License-Identifier: GPL-3.0-or-later
// Hides the menu entries that only open discontinued web pages: the full story
// video in the main menu (a redirect that no longer leads to it). The game builds
// it only with buildSettings.trailerLink.
#include "common/game_scripts.hpp"
#include "mod.hpp"

namespace {

constexpr const char* kGame = R"(
local initialize = initializeGame
function initializeGame(...)
    local results = {initialize(...)}
    buildSettings.trailerLink = false
    return unpack(results)
end
)";

class HideDeadServices final : public slingshot::Mod {
public:
    HideDeadServices() { slingshot::mods::append_lua("data/scripts/game.lua", kGame); }
};

}  // namespace

SLINGSHOT_MOD(HideDeadServices, "hide-dead-services", "Hide the full story video, whose web page is gone");
