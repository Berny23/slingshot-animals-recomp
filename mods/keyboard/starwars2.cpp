// SPDX-License-Identifier: GPL-3.0-or-later
// keyboard for Star Wars II (see keyboard.hpp).
#include "keyboard/keyboard.hpp"

namespace {

constexpr const char* kAdapter = R"(
local BAND = 0.405  -- the rubber band's full stretch, physics units, until shootMaxLength is set

local function root() return menuManager and menuManager.currentRoot end
function KB.playing()
    local r = root()
    local hud = r and r:getChild('gameHud')
    if not hud or not isInGameMode() or levelCompleted or levelFailed then return false end
    local pause = hud:getChild('pausePage')
    return not (pause and pause.visible)
end
function KB.bird()
    if not birdReady or dragStarted then return nil end  -- birdSelected stays set after the first shot
    local bird = currentBirdName and objects and objects.world and objects.world[currentBirdName]
    if not bird then return nil end
    local x, y = physicsToScreenTransform(bird.x, bird.y)
    local x2 = physicsToScreenTransform(bird.x + 1, bird.y)
    return x, y, (shootMaxLength or BAND) * _G.math.abs(x2 - x)
end
-- flyingBird stays set after the bird landed.
function KB.flying() return not birdReady and flyingBird ~= nil and birdSpecialtyAvailable == true end
function KB.level() return levelName end
local VALUES = {restart = 'RESTART_LEVEL', next = 'NEXT_LEVEL'}
function KB.button(name) return find_button(root(), VALUES[name]) end
function KB.zoom() toggleZoom() end
)";

}  // namespace

SLINGSHOT_KEYBOARD_MOD(std::string(slingshot::mods::keyboard::kFrameButtonsLua) + kAdapter);
