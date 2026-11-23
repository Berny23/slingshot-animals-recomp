// SPDX-License-Identifier: GPL-3.0-or-later
// Every episode is open from its first level, as in the early versions. The
// planets after Tatooine (normalLevelFolders) open once Tatooine is completed
// (isEpisodeDisabled in game.lua); here they are open from the start. The
// purchased episodes and the droid levels keep their own conditions.
#include "common/game_scripts.hpp"
#include "mod.hpp"

namespace {

constexpr const char* kGame = R"(
local disabled = isEpisodeDisabled
function isEpisodeDisabled(folder, ...)
    local folders = actions and actions.myLevelFolders and actions.myLevelFolders.normalLevelFolders
    for _, f in ipairs(folders or {}) do
        if f == folder then return false end
    end
    return disabled(folder, ...)
end
)";

class UnlockEpisodes final : public slingshot::Mod {
public:
    UnlockEpisodes() { slingshot::mods::append_lua("data/scripts/game.lua", kGame); }
};

}  // namespace

SLINGSHOT_MOD_OFF(UnlockEpisodes, "unlock-episodes", "Every episode can be played from its first level");
