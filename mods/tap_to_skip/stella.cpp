// SPDX-License-Identifier: GPL-3.0-or-later
// tap-to-skip: a tap skips the wait at the end of a level.
//
// The waits are g_levelEndLogic's. A won level counts the remaining birds every
// birdBuffTimer, then waits levelEndTimer; a tap on no HUD button counts them all
// at once and ends the wait. A locked bird stops the count there: its unlock
// prompt comes as without the tap. Once every bird has hit something, the game
// fails the level when no points came for a while (scoreAddedTime below 0); a
// tap makes the last points that old and ends the wait after it.
#include "common/game_scripts.hpp"
#include "mod.hpp"

namespace {

constexpr const char* kLua = R"(
local function birds_landed()
    if birdReady then return false end
    for _, bird in pairs(birds or {}) do
        if bird.controllable and not bird.hasCollided then return false end
    end
    return true
end
local function skip_won(ending)
    for _ = 1, 100 do
        local name = getNextBird(ending.birdsLeftCounter)
        if not name then
            ending.birdBuffTimer = 0
            ending.levelEndTimer = math.min(ending.levelEndTimer, 0.001)
            return
        end
        if objects.world[name]:isLocked() then
            ending.birdBuffTimer = 0
            return
        end
        ending:_handleUnusedBirdScore(name)
        ending.birdsLeftCounter = ending.birdsLeftCounter + 1
    end
end

local update = ui.GameHud.update
function ui.GameHud:update(...)
    local ending = g_levelEndLogic
    if self.skipping and ending then
        self.skipping = not g_levelCompleted and not ending.levelEndScreenTriggered and birds_landed()
        if self.skipping then
            ending.scoreAddedTime = math.min(ending.scoreAddedTime or 0, -1)
            if g_levelFailed then ending.levelEndTimer = math.min(ending.levelEndTimer, 0.001) end
        end
    end
    return update(self, ...)
end
local pointer = ui.GameHud.onPointerEvent
function ui.GameHud:onPointerEvent(event, x, y, ...)
    local results = {pointer(self, event, x, y, ...)}
    local ending = g_levelEndLogic
    if event ~= 'LRELEASE' or results[1] ~= nil or not ending or ending.levelEndScreenTriggered
        or (self:getChild('pausePage') or {}).visible then
        return unpack(results)
    end
    if flyingBird and birdSpecialtyAvailable then  -- that touch is the ability
    elseif g_levelCompleted then
        skip_won(ending)
    elseif birds_landed() then
        self.skipping = true
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
