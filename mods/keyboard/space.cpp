// SPDX-License-Identifier: GPL-3.0-or-later
// keyboard for Space (see keyboard.hpp).
#include "keyboard/keyboard.hpp"

namespace {

constexpr const char* kAdapter = R"(
local BAND = 5.4  -- the rubber band's full stretch, physics units

local function menus() return GameSystem and GameSystem.menuManager end
function KB.playing()
    local top = menus() and menus():getTopFrame()
    if not top or top.name ~= 'gameHud' or levelCompleted or levelFailed then return false end
    local pause = top:getChild('pausePage')
    return not (pause and pause.visible)
end
function KB.bird()
    if not birdReady or selectedBird then return nil end
    local bird = currentBirdName and objects and objects.world and objects.world[currentBirdName]
    if not bird then return nil end
    local x, y = physicsToScreenTransform(bird.x, bird.y)
    local x2 = physicsToScreenTransform(bird.x + 1, bird.y)
    return x, y, BAND * _G.math.abs(x2 - x)
end
-- flyingBird stays set after the bird landed.
function KB.flying() return not birdReady and flyingBird ~= nil and birdSpecialtyAvailable == true end
function KB.level() return levelName end
local VALUES = {restart = 'RESTART_LEVEL', next = 'NEXT_LEVEL'}
function KB.button(name)
    return find_button(menus() and menus():getRoot(), VALUES[name])
end
)";

}  // namespace

SLINGSHOT_KEYBOARD_MOD(std::string(slingshot::mods::keyboard::kFrameButtonsLua) + kAdapter);
