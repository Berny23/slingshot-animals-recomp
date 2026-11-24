// SPDX-License-Identifier: GPL-3.0-or-later
// Ragnahog's levels open one after another, as in the other episodes.
//
// Ragnahog is an advent calendar: its episode has manual_level_progression,
// so completing a level opens nothing, and checkCalendarLogic opens a level
// per day of December 2016 (all of them after the 25th). That runs only once
// Rovio's time server has answered (dateUpdated), so offline only the first
// level was ever open. Without the flag, the game's own progression applies:
// a completed level opens the next one (eventTriggered), and
// checkLastOpenLevels opens those after the levels already completed. The
// golden eggs and the three-star level keep their own conditions.
#include "common/game_scripts.hpp"
#include "mod.hpp"

namespace {

// episodes.lua builds the episode tables as it runs and again in each of these
// functions, which it also stores in gamelua.
constexpr const char* kEpisodes = R"(
local env = getfenv(1)
local function progress()
    local episode = getLocalEpisode('episodeRagnahog')
    if episode then episode.manual_level_progression = nil end
end
for _, name in ipairs({'initializeEpisodeTables', 'resetEpisodeTables', 'refreshEpisodeTables'}) do
    local original = env[name]
    if original then
        local function wrapped(...)
            local results = {original(...)}
            progress()
            return unpack(results)
        end
        env[name] = wrapped
        if gamelua and gamelua[name] == original then gamelua[name] = wrapped end
    end
end
progress()
)";

class RagnahogProgression final : public slingshot::Mod {
public:
    RagnahogProgression() { slingshot::mods::append_lua("data/scripts/episodes.lua", kEpisodes); }
};

}  // namespace

SLINGSHOT_MOD(RagnahogProgression, "ragnahog-progression",
              "Ragnahog's levels open one after another instead of by the dates of December 2016");
