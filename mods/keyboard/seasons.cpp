// SPDX-License-Identifier: GPL-3.0-or-later
// keyboard for Seasons (see keyboard.hpp).
#include "keyboard/keyboard.hpp"

namespace {

constexpr const char* kAdapter = R"(
local BAND = 5.4  -- the rubber band's full stretch, physics units

local function root() return menuManager and menuManager.menus and menuManager.menus.root end
local function hud()
    local r = root()
    return r and r.name == 'gameHud' and r or nil
end
local function popup_open()
    for _, child in _G.ipairs(notificationsFrame and notificationsFrame.children or {}) do
        if child.visible ~= false and child.name ~= 'IngameCurrencyFrame' and child.name ~= 'loading' then return true end
    end
    return false
end
function KB.playing()
    local h = hud()
    if not h or skipInput or levelCompleted or popup_open() then return false end
    local pause = h:getChild('pausePage')
    return not (pause and pause.visible)
end
function KB.bird()
    if not birdReady or birdSelected then return nil end
    local bird = currentBirdName and objects and objects.world and objects.world[currentBirdName]
    if not bird then return nil end
    local x, y = physicsToScreenTransform(bird.x, bird.y)
    local x2 = physicsToScreenTransform(bird.x + 1, bird.y)
    return x, y, BAND * (g_slingshot_power or 1) * _G.math.abs(x2 - x)
end
-- flyingBird stays set after the bird landed.
function KB.flying() return not birdReady and flyingBird ~= nil and birdSpecialtyAvailable == true end
function KB.level() return levelName end
local VALUES = {restart = 'RESTART_LEVEL', next = 'NEXT_LEVEL'}
function KB.button(name)
    local x, y = find_button(notificationsFrame, VALUES[name])
    if x then return x, y end
    return find_button(root(), VALUES[name])
end
function KB.zoom() if toggleZoom then toggleZoom() end end
)";

}  // namespace

SLINGSHOT_KEYBOARD_MOD(std::string(slingshot::mods::keyboard::kFrameButtonsLua) + kAdapter);
