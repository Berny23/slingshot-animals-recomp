// SPDX-License-Identifier: GPL-3.0-or-later
// Every episode is open from its first level, as in the early versions. A chapter
// (normalLevelFolders, one folder per side) opens after the one before it, its
// other side with stars (starsToUnlockOtherSide), or either for credits
// (isEpisodeDisabled, isEpisodeSideDisabled in game.lua); here both sides of every
// chapter are open. The purchased chapter and the bonus levels keep their own
// conditions.
#include "common/game_scripts.hpp"
#include "mod.hpp"

namespace {

constexpr const char* kGame = R"(
local function regular(folder)
    local folders = actions and actions.myLevelFolders and actions.myLevelFolders.normalLevelFolders
    for _, f in ipairs(folders or {}) do
        if f == folder then return true end
    end
    return false
end
local disabled = isEpisodeDisabled
function isEpisodeDisabled(folder, ...)
    if regular(folder) then return false end
    return disabled(folder, ...)
end
local side_disabled = isEpisodeSideDisabled
function isEpisodeSideDisabled(folder, ...)
    if regular(folder) then return false, 0 end
    return side_disabled(folder, ...)
end
)";

class UnlockEpisodes final : public slingshot::Mod {
public:
    UnlockEpisodes() { slingshot::mods::append_lua("data/scripts/game.lua", kGame); }
};

}  // namespace

SLINGSHOT_MOD_OFF(UnlockEpisodes, "unlock-episodes", "Every episode can be played from its first level");
