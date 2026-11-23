// SPDX-License-Identifier: GPL-3.0-or-later
// Every episode is open from its first level, as in the early versions. The
// episodes not unlockedByDefault are bought with coins (buyEpisodePopup:
// settingsWrapper:unlockEpisode); here they are unlocked the same way when the
// game first asks for them (isEpisodeUnlocked), and the next save keeps it. Left
// out: the Intel levels, reached from Winter Wonderham, and the prototype levels.
#include "common/game_scripts.hpp"
#include "mod.hpp"

namespace {

constexpr const char* kSettingsWrapper = R"(
local skip = {episodeIntel = true, episodePrototypes = true}
local unlocked = settingsWrapper.isEpisodeUnlocked
function settingsWrapper:isEpisodeUnlocked(episode, ...)
    if not unlocked(self, episode, ...) and not skip[episode] then
        local get = getAnyEpisode or (gamelua and gamelua.getAnyEpisode)
        if get and get(episode) then self:unlockEpisode(episode) end
    end
    return unlocked(self, episode, ...)
end
)";

class UnlockEpisodes final : public slingshot::Mod {
public:
    UnlockEpisodes() { slingshot::mods::append_lua("data/scripts/settingsWrapper.lua", kSettingsWrapper); }
};

}  // namespace

SLINGSHOT_MOD_OFF(UnlockEpisodes, "unlock-episodes", "Every episode can be played from its first level");
