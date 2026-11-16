// SPDX-License-Identifier: GPL-3.0-or-later
// tap-to-skip: a tap skips the wait at the end of a level.
//
// A won level counts the remaining birds every 0.5 s (updateLevelEnding); a tap
// counts them all at once. With all birds shot, the game waits until nothing
// moves any more (checkLevelFailed) or no points came for 15 s (g_levelTimeout);
// once the last bird has hit something, a tap makes the last points that old. Taps on
// the HUD strip at the top left (pause, Mighty Eagle, power-ups) stay what they
// are, and so does a tap that is the flying bird's ability.
#include <string>

#include "common/game_scripts.hpp"
#include "common/script_link.hpp"
#include "mod.hpp"
#include "recomp/game.hpp"

namespace {

constexpr const char* kLua = R"(
local HUD_W, HUD_H = 0.5, 0.16  -- the HUD strip, fractions of the screen

local function popup_open()
    local engine = menuengine and menuengine.instance
    return engine ~= nil and engine.popupManager ~= nil and engine.popupManager:isPopupVisible()
end
local function skip_won()
    for _ = 1, 100 do
        if not getNextBird(birdsLeftCounter) then break end
        birdBuffTimer = -1  -- the next bird's turn
        updateLevelEnding(0)
    end
    levelCompleteTimer = G.math.min(levelCompleteTimer or 0, 0.001)
end
local function last_bird_landed()
    if birdReady or not flyingBird or not flyingBird.hasCollided then return false end
    for _, bird in G.pairs(birds or {}) do
        if not bird.shot then return false end
    end
    return true
end
local skipping = false
local function hold_timeout()
    g_oldScore = score
    g_lastScoreTime = -1e6  -- 0 would mean no timeout
end
each_frame(function()
    if skipping then
        skipping = not levelCompleted and last_bird_landed()
        if skipping then hold_timeout() end
    end
    if not (keyReleased and keyReleased.LBUTTON) then return end
    if not (sm and sm.scenes and sm.currentScene == sm.scenes.game) or popup_open() then return end
    if cursor.x < screenWidth * HUD_W and cursor.y < screenHeight * HUD_H then return end
    if flyingBird and birdSpecialtyAvailable then return end  -- that tap is the ability
    if levelCompleted then
        if (levelCompleteTimer or 0) > 0 then skip_won() end
    elseif last_bird_landed() then
        skipping = true
        hold_timeout()
    end
end)
)";

class TapToSkip final : public slingshot::Mod {
public:
    TapToSkip() {
        slingshot::mods::append_lua(recomp::game.main_script, std::string(slingshot::mods::kEachFrameLua) + kLua);
    }
};

}  // namespace

SLINGSHOT_MOD(TapToSkip, "tap-to-skip", "Tap anywhere to skip the wait at the end of a level");
