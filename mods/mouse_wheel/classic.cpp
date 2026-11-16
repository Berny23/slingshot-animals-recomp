// SPDX-License-Identifier: GPL-3.0-or-later
// mouse-wheel for Classic (see mouse_wheel.hpp).
#include "mouse_wheel/mouse_wheel.hpp"

namespace {

constexpr const char* kAdapter = R"(
function WH.in_level()
    local root = menuManager and menuManager.menus and menuManager.menus.root
    return root ~= nil and root.name == 'gameHud'
end
)";

// SmartScrollFrame (episode and level maps) moves one anchor per event; here it
// moves by the notches, once per wheel event.
constexpr const char* kSmartScrollFrame = R"(
local pointer = SmartScrollFrame.onPointerEvent
function SmartScrollFrame:onPointerEvent(event, x, y, ...)
    if event ~= 'WHEEL' then return pointer(self, event, x, y, ...) end
    local notches = gamelua.cursor.wheel or 0
    gamelua.cursor.wheel = 0  -- the original's next/previous step is replaced below
    local results = {pointer(self, event, x, y, ...)}
    gamelua.cursor.wheel = notches
    local id = gamelua.cursor.wheelId
    if notches ~= 0 and id ~= self.wheelId and #(self.anchors or {}) > 0 and self:isWithinFrame(x, y) then
        self.wheelId = id
        self:setTargetAnchor((self.targetAnchor or self.currentAnchor or 1) - notches)
        self.velocity = 8  -- as next/previous
    end
    return unpack(results)
end
)";

// The sticker book's tabs page with swipes and arrows (prevSubPage/nextSubPage);
// a notch turns a page.
constexpr const char* kStickerBook = R"(
local pointer = StickerBookPageCommon.onPointerEvent
function StickerBookPageCommon:onPointerEvent(event, x, y, ...)
    if event ~= 'WHEEL' then return pointer(self, event, x, y, ...) end
    local cursor = gamelua.cursor
    local count = #(self.stickerSubPages or {})
    if cursor.wheel ~= 0 and cursor.wheelId ~= self.wheelId and count > 1 then
        self.wheelId = cursor.wheelId
        local page = math.max(1, math.min(count, (self.currentSubPageNumber or 1) - cursor.wheel))
        if page ~= self.currentSubPageNumber then self:switchToSubPage(page) end
    end
    return pointer(self, event, x, y, ...)
end
)";

}  // namespace

SLINGSHOT_MOUSE_WHEEL_MOD(kAdapter, {{"data/scripts/ui_components/SmartScrollFrame.lua", kSmartScrollFrame},
                                     {"data/scripts/ui_components/ScrollFrame2.lua",
                                      slingshot::mods::mouse_wheel::kScrollFrameLua},
                                     {"data/scripts/menus/StickerBookPageCommon.lua", kStickerBook}});
