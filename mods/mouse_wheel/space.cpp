// SPDX-License-Identifier: GPL-3.0-or-later
// mouse-wheel for Space (see mouse_wheel.hpp).
#include "mouse_wheel/mouse_wheel.hpp"

namespace {

constexpr const char* kAdapter = R"(
function WH.in_level()
    local menus = GameSystem and GameSystem.menuManager
    local top = menus and menus:getTopFrame()
    return top ~= nil and top.name == 'gameHud'
end
)";

// The episode carousel steps one episode per WHEEL event from where its 0.5 s
// animation is, so notches that come while it moves are lost. Here they add up
// on the target.
constexpr const char* kEpisodeSelection = R"(
local ANIMATION_TIME = 0.5  -- the carousel's own
local Selection = menu.EpisodeSelection
local update = Selection.update
function Selection:update(dt, ...)
    self.wheelAge = (self.wheelAge or 0) + (dt or 0)
    return update(self, dt, ...)
end
local pointer = Selection.onPointerEvent
function Selection:onPointerEvent(event, x, y, ...)
    if event ~= 'WHEEL' then return pointer(self, event, x, y, ...) end
    local notches = cursor.wheel or 0
    if notches ~= 0 and cursor.wheelId ~= self.wheelId and not self._drag then
        self.wheelId = cursor.wheelId
        local moving = self.wheelTarget and self.wheelAge < ANIMATION_TIME
        local from = moving and self.wheelTarget or math.floor(self._currentAnchor + 0.5)
        self.wheelTarget, self.wheelAge = from + notches, 0
        self:getChild('animator'):setAnimation({_currentAnchor = self.wheelTarget}, ANIMATION_TIME,
                                               ui.FrameAnimator.cubicEaseOut)
    end
    cursor.wheel = 0  -- the original's step is replaced above
    local results = {pointer(self, event, x, y, ...)}
    cursor.wheel = notches
    return unpack(results)
end
)";

// ui.ScrollFrameFree (the tidbits, the Solar System selector) only drags.
constexpr const char* kScrollFrameFree = R"(
local STEP = 0.2  -- of the screen along the scroll axis, per notch
local pointer = ui.ScrollFrameFree.onPointerEvent
function ui.ScrollFrameFree:onPointerEvent(event, x, y, ...)
    local results = {pointer(self, event, x, y, ...)}
    local notches = cursor.wheel or 0
    local clip = self.clip  -- as for a drag: inside it
    local inside = not clip or (cursor.x >= clip.x and cursor.y >= clip.y
                                and cursor.x < clip.x + clip.w and cursor.y < clip.y + clip.h)
    if event == 'WHEEL' and notches ~= 0 and cursor.wheelId ~= self.wheelId and not self.dragging and inside then
        self.wheelId = cursor.wheelId
        local axis = self.axis
        local size = axis == 'x' and screenWidth or screenHeight
        self.scroll[axis] = math.max(self.scroll_min, math.min(self.scroll_max, self.scroll[axis] - notches * STEP * size))
        self.velocity[axis] = 0
    end
    return unpack(results)
end
)";

const std::string kGameScene = slingshot::mods::mouse_wheel::camera_pages_lua(
    "not isPCVersion() and not isInGameMode() and not (g_gesturePausePage and g_gesturePausePage.gestureMenuTriggered > 0)",
    "{'contentFrame'}");

}  // namespace

SLINGSHOT_MOUSE_WHEEL_MOD(kAdapter, {{"data/scripts/ui/ScrollFrame.lua", slingshot::mods::mouse_wheel::kScrollFrameLua},
                                     {"data/scripts/menu/EpisodeSelection.lua", kEpisodeSelection},
                                     {"data/scripts/gamescene.lua", kGameScene.c_str()},
                                     {"data/scripts/ui/ScrollFrameFree.lua", kScrollFrameFree}});
