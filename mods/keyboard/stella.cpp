// SPDX-License-Identifier: GPL-3.0-or-later
// keyboard for Stella (see keyboard.hpp).
#include "keyboard/keyboard.hpp"

namespace {

constexpr const char* kAdapter = R"(
local BAND = 1.08  -- a full stretch, in g_slingshot.rubberBandRadius

local function root() return menuManager and menuManager.currentRoot end
function KB.playing()
    local r = root()
    local hud = r and r:getChild('gameHud')
    if not hud or not isInGameMode() or g_levelCompleted or g_levelFailed then return false end
    local pause = hud:getChild('pausePage')
    return not (pause and pause.visible)
end
function KB.bird()
    if not birdReady or dragStarted or not g_slingshot then return nil end
    local bird = currentBirdName and objects and objects.world and objects.world[currentBirdName]
    if not bird then return nil end
    local x, y = physicsToScreenTransform(bird.x, bird.y)
    local x2 = physicsToScreenTransform(bird.x + 1, bird.y)
    return x, y, BAND * g_slingshot.rubberBandRadius * _G.math.abs(x2 - x)
end
-- flyingBird stays set after the bird landed.
function KB.flying() return not birdReady and flyingBird ~= nil and birdSpecialtyAvailable == true end
function KB.level() return levelName end
local VALUES = {restart = 'RESTART_LEVEL', next = 'NEXT_LEVEL'}
function KB.button(name) return find_button(root(), VALUES[name]) end
)";

}  // namespace

SLINGSHOT_KEYBOARD_MOD(std::string(slingshot::mods::keyboard::kFrameButtonsLua) + kAdapter);
