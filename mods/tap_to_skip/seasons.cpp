// SPDX-License-Identifier: GPL-3.0-or-later
// tap-to-skip: a tap skips the wait at the end of a level.
//
// A won level counts the remaining birds one by one; a tap on no HUD button does
// what the HUD's skip button does (skipToLevelEnd), which Seasons never shows.
// With all birds shot, the game waits levelEndTimer (10 s, the extra bird
// drawer) before it offers more birds ("Continue playing?"); a tap ends that
// wait, AFTER_LAST_BIRD seconds after it began, so the last bird's ability still
// works.
#include "common/game_scripts.hpp"
#include "mod.hpp"

namespace {

constexpr const char* kLua = R"(
local AFTER_LAST_BIRD = 1.5  -- seconds

local function skip()
    local g = gamelua
    if g.g_skip_to_level_end or g.skipInput then return false end
    if g.flyingBird and g.birdSpecialtyAvailable then return false end  -- that tap is the ability
    if g.levelCompleted then
        if (g.levelCompleteTimer or 0) <= 0 then return false end
        g.skipToLevelEnd()
        return true
    end
    local wait = (g.LEVEL_ENDING_EXTRABIRD_DELAY_TIME or 0) + (g.LEVEL_ENDING_EXTRABIRD_ACTIVATE_TIME or 0)
        + (g.LEVEL_ENDING_EXTRABIRD_HURRY_TIME or 0)
    local left = g.levelEndTimer
    if g.birdReady or not left or left <= 0 or left > wait - AFTER_LAST_BIRD then return false end
    g.levelEndTimer = 0.001
    return true
end

local pointer = GameHud.onPointerEvent
function GameHud:onPointerEvent(event, x, y, ...)
    local results = {pointer(self, event, x, y, ...)}
    if event == 'LRELEASE' and results[1] == nil and not (self:getChild('pausePage') or {}).visible then
        skip()
    end
    return unpack(results)
end
)";

class TapToSkip final : public slingshot::Mod {
public:
    TapToSkip() { slingshot::mods::append_lua("data/scripts/menus/GameHud.lua", kLua); }
};

}  // namespace

SLINGSHOT_MOD(TapToSkip, "tap-to-skip", "Tap anywhere to skip the wait at the end of a level");
