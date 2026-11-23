// SPDX-License-Identifier: GPL-3.0-or-later
// Every episode is open from its first level, as in the early versions. The
// regular episodes after the first open once one of their levels was played
// (is_unlocked in levelQueryUtils.lua: isAnyLevelPlayed) or bought; the first
// level of each is open from the start. The hidden episode (BTN_EP_HIDDEN) still
// needs its key.
#include "common/game_scripts.hpp"
#include "mod.hpp"

namespace {

constexpr const char* kLevelQueryUtils = R"(
local unlocked = get_regular_episode_unlocked
if unlocked then
    function get_regular_episode_unlocked(index, ...)
        if get_regular_episode_button_sprite(index) ~= 'BTN_EP_HIDDEN' then return true end
        return unlocked(index, ...)
    end
end
)";

class UnlockEpisodes final : public slingshot::Mod {
public:
    UnlockEpisodes() { slingshot::mods::append_lua("data/scripts/levelQueryUtils.lua", kLevelQueryUtils); }
};

}  // namespace

SLINGSHOT_MOD_OFF(UnlockEpisodes, "unlock-episodes", "Every episode can be played from its first level");
