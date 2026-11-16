// SPDX-License-Identifier: GPL-3.0-or-later
// tap-to-skip: a tap skips the wait at the end of a level.
//
// A won level counts the remaining birds every 0.5 s (updateLevelEnding); a tap
// on no HUD button counts them all at once. Once every bird has hit something,
// the game fails the level when no points came for a while (g_scoreAddedTime
// below 0); a tap makes the last points that old.
#include "common/game_scripts.hpp"
#include "mod.hpp"

namespace {

constexpr const char* kLua = R"(
local G = _G
local function birds_landed(g)
    if g.birdReady then return false end
    for _, bird in G.pairs(g.birds or {}) do
        if bird.controllable and not bird.hasCollided then return false end
    end
    return true
end
local function skip_won(g)
    for _ = 1, 100 do
        if not g.getNextBird(g.birdsLeftCounter) then break end
        g.birdBuffTimer = -1  -- the next bird's turn
        g.updateLevelEnding(0)
    end
    g.levelCompleteTimer = G.math.min(g.levelCompleteTimer or 0, 0.001)
end

local update = GameHud.update
function GameHud:update(...)
    local g = gamelua
    if self.skipping then
        self.skipping = not g.levelCompleted and not g.levelFailed and birds_landed(g)
        if self.skipping then g.g_scoreAddedTime = -1 end
    end
    return update(self, ...)
end
local pointer = GameHud.onPointerEvent
function GameHud:onPointerEvent(event, x, y, ...)
    local results = {pointer(self, event, x, y, ...)}
    local g = gamelua
    if event ~= 'LRELEASE' or results[1] ~= nil or (self:getChild('pausePage') or {}).visible
        or not g.isInGameMode() then
        return G.unpack(results)
    end
    if g.flyingBird and g.birdSpecialtyAvailable then  -- that tap is the ability
    elseif g.levelCompleted then
        if (g.levelCompleteTimer or 0) > 0 then skip_won(g) end
    elseif not g.levelFailed and birds_landed(g) then
        self.skipping = true
        g.g_scoreAddedTime = -1
    end
    return G.unpack(results)
end
)";

class TapToSkip final : public slingshot::Mod {
public:
    TapToSkip() { slingshot::mods::append_lua("data/scripts/menus/GameHud.lua", kLua); }
};

}  // namespace

SLINGSHOT_MOD(TapToSkip, "tap-to-skip", "Tap anywhere to skip the wait at the end of a level");
