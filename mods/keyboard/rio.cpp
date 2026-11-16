// SPDX-License-Identifier: GPL-3.0-or-later
// keyboard for Rio (see keyboard.hpp).
#include "keyboard/keyboard.hpp"

namespace {

constexpr const char* kAdapter = R"(
local BAND = 5.4  -- the rubber band's full stretch, physics units

local function in_scene(name) return sm ~= nil and sm.scenes ~= nil and sm.currentScene == sm.scenes[name] end
local function popup_open()
    local engine = menuengine and menuengine.instance
    return engine ~= nil and engine.popupManager ~= nil and engine.popupManager:isPopupVisible()
end
function KB.playing() return in_scene('game') and not levelCompleted and not popup_open() end
function KB.bird()
    if not birdReady or birdSelected then return nil end
    local bird = currentBirdName and objects and objects.world and objects.world[currentBirdName]
    if not bird then return nil end
    local x, y = physicsToScreenTransform(bird.x, bird.y)
    local x2 = physicsToScreenTransform(bird.x + 1, bird.y)
    return x, y, BAND * _G.math.abs(x2 - x)
end
-- flyingBird stays set after the bird landed.
function KB.flying() return not birdReady and flyingBird ~= nil and birdSpecialtyAvailable == true end
function KB.level() return levelName end
local ITEMS = {restart = 'restart', next = 'nextLevel'}
function KB.button(name)
    local scene = sm and sm.currentScene
    for _, page in _G.pairs(scene and scene.pages or {}) do
        local item = page.visible and page.items and page.items[ITEMS[name]]
        if item and item.visible ~= false and item.enabled ~= false and item.action then
            return (page.x or 0) + (item.x or 0), (page.y or 0) + (item.y or 0)
        end
    end
end
function KB.zoom() if toggleZoom then toggleZoom() end end
)";

}  // namespace

SLINGSHOT_KEYBOARD_MOD(kAdapter);
