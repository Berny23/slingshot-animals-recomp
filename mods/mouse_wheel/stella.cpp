// SPDX-License-Identifier: GPL-3.0-or-later
// mouse-wheel for Stella (see mouse_wheel.hpp).
#include "mouse_wheel/mouse_wheel.hpp"

namespace {

constexpr const char* kAdapter = R"(
function WH.in_level() return isInGameMode ~= nil and isInGameMode() end
)";

// Wheel notches, once per wheel event, for `frame` while no shown frame lies
// over it (a popup: a later child of the scene that is no button).
constexpr const char* kNotchesLua = R"(
local function notches(frame)
    if not cursor.wheelTriggered or cursor.wheelId == frame.wheelId then return 0 end
    frame.wheelId = cursor.wheelId
    local children, above = menuManager.currentRoot.children or {}, false
    for _, child in ipairs(children) do
        if above and child.visible ~= false and not child.returnValue and not tostring(child.name):find('^BLOCK_') then
            return 0
        end
        above = above or child == frame
    end
    return above and cursor.wheel or 0
end
)";

// The level map's camera moves along a path, by the drag that MapScreen.update
// reads from dragCursorTable while dragStarted. A notch is a sideways drag of
// STEP screen widths, spread over a few frames.
constexpr const char* kMapScreen = R"(
local STEP = 0.2
local SHARE = 0.35  -- of the rest per frame
local update = ui.MapScreen.update
function ui.MapScreen:update(...)
    self.wheelRest = (self.wheelRest or 0) + notches(self) * STEP * screenWidth
    local rest = self.wheelRest
    if dragStarted or cameraDisabled or math.abs(rest) < 1 then
        self.wheelRest = 0
        return update(self, ...)
    end
    local step = rest * SHARE
    self.wheelRest = rest - step
    local finger = dragCursorTable[dragCursorIndex]
    dragCursorTable[dragCursorIndex] = {x = finger and finger.x or 0, y = finger and finger.y or 0, dx = step, dy = 0}
    dragStarted = true
    local results = {pcall(update, self, ...)}
    dragStarted = false
    dragCursorTable[dragCursorIndex] = finger
    if not results[1] then error(results[2], 0) end
    return unpack(results, 2)
end
)";

// The scrapbook turns a page per wheel event (its bird pages only), and drops
// those that come while a page turns. Here the scrapbook and the Piggypedia turn
// with gotoNextPage/gotoPreviousPage once canTurnPage allows it; up to
// MAX_QUEUED notches wait for that.
constexpr const char* kScrapbook = R"(
local MAX_QUEUED = 3
local update = ui.Scrapbook.update
function ui.Scrapbook:update(...)
    local queued = math.max(-MAX_QUEUED, math.min(MAX_QUEUED, (self.wheelQueue or 0) + notches(self)))
    if queued ~= 0 and self:canTurnPage() then
        if queued < 0 then self:gotoNextPage() else self:gotoPreviousPage() end
        queued = queued - (queued < 0 and -1 or 1)
    end
    self.wheelQueue = queued
    return update(self, ...)
end
local pointer = ui.Scrapbook.onPointerEvent
function ui.Scrapbook:onPointerEvent(event, ...)
    if event ~= 'WHEEL' then return pointer(self, event, ...) end
    local wheel = cursor.wheel
    cursor.wheel = 0  -- the turns are made above
    local results = {pcall(pointer, self, event, ...)}
    cursor.wheel = wheel
    if not results[1] then error(results[2], 0) end
    return unpack(results, 2)
end
)";

const std::string kMapLua = std::string(kNotchesLua) + kMapScreen;
const std::string kScrapbookLua = std::string(kNotchesLua) + kScrapbook;

}  // namespace

SLINGSHOT_MOUSE_WHEEL_MOD(kAdapter, {{"data/scripts_common/ui_components/ScrollFrame.lua",
                                      slingshot::mods::mouse_wheel::kScrollFrameLua},
                                     {"data/scripts/menus/map_screen/MapScreen.lua", kMapLua.c_str()},
                                     {"data/scripts/menus/scrapbook/Scrapbook.lua", kScrapbookLua.c_str()}});
