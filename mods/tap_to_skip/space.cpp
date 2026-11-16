// SPDX-License-Identifier: GPL-3.0-or-later
// tap-to-skip: a tap skips the wait at the end of a level.
//
// A won level counts the remaining birds every 0.5 s (the level mode's
// updateLevelEnding); a tap on no HUD button counts them all at once. Once every
// bird has hit something, the game fails the level when no points came for a
// while (g_scoreAddedTime below 0); a tap makes the last points that old.
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
local function skip_won(mode)
    for _ = 1, 100 do
        if not getNextBird(birdsLeftCounter) then break end
        birdBuffTimer = -1  -- the next bird's turn
        mode:updateLevelEnding(0)
    end
    levelCompleteTimer = math.min(levelCompleteTimer or 0, 0.001)
end

local Hud = menu.GameHud
local update = Hud.update
function Hud:update(...)
    if self.skipping then
        self.skipping = not levelCompleted and not levelFailed and birds_landed()
        if self.skipping then g_scoreAddedTime = -1 end
    end
    return update(self, ...)
end
local pointer = Hud.onPointerEvent
function Hud:onPointerEvent(event, x, y, ...)
    local results = {pointer(self, event, x, y, ...)}
    if event ~= 'LRELEASE' or results[1] ~= nil or (self:getChild('pausePage') or {}).visible then
        return unpack(results)
    end
    local mode = objects and objects.levelMode
    if flyingBird and birdSpecialtyAvailable then  -- that tap is the ability
    elseif levelCompleted then
        if (levelCompleteTimer or 0) > 0 and mode and mode.updateLevelEnding then skip_won(mode) end
    elseif not levelFailed and birds_landed() then
        self.skipping = true
        g_scoreAddedTime = -1
    end
    return unpack(results)
end
)";

class TapToSkip final : public slingshot::Mod {
public:
    TapToSkip() { slingshot::mods::append_lua("data/scripts/menu/GameHud.lua", kLua); }
};

}  // namespace

SLINGSHOT_MOD(TapToSkip, "tap-to-skip", "Tap anywhere to skip the wait at the end of a level");
