// SPDX-License-Identifier: GPL-3.0-or-later
// Every episode is open from its first level, as in the early versions. An
// episode opens when its first level is open (isEpisodeOpen), which takes the
// stars in its starsRequired. settingsWrapper:unlockEpisode opens that level, as
// for the timed episode releases (EpisodeLocking), and the next save keeps it.
// The levels after it open as usual.
#include "common/game_scripts.hpp"
#include "mod.hpp"

namespace {

constexpr const char* kSettingsWrapper = R"(
local open = settingsWrapper.isEpisodeOpen
function settingsWrapper:isEpisodeOpen(episode, ...)
    if episode ~= 'G' and settings and g_episodes and g_episodes[episode] then self:unlockEpisode(episode) end
    return open(self, episode, ...)
end
)";

class UnlockEpisodes final : public slingshot::Mod {
public:
    UnlockEpisodes() { slingshot::mods::append_lua("data/scripts/settingsWrapper.lua", kSettingsWrapper); }
};

}  // namespace

SLINGSHOT_MOD_OFF(UnlockEpisodes, "unlock-episodes", "Every episode can be played from its first level");
