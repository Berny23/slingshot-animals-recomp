// SPDX-License-Identifier: GPL-3.0-or-later
// mouse-wheel for Seasons (see mouse_wheel.hpp).
#include "mouse_wheel/mouse_wheel.hpp"

namespace {

constexpr const char* kAdapter = R"(
function WH.in_level()
    local root = menuManager and menuManager.menus and menuManager.menus.root
    return root ~= nil and root.name == 'gameHud'
end
)";

// The Pig Challenge's two pages, which a swipe changes: the wheel turns them,
// and on the weekend page it scrolls the leaderboard (its right part).
constexpr const char* kTournamentMenu = R"(
local BOARD_LEFT = 0.6  -- of the screen width
local pointer = SeasonsTournamentMenu.onPointerEvent
function SeasonsTournamentMenu:onPointerEvent(event, x, y, ...)
    local cursor = gamelua.cursor
    if event ~= 'WHEEL' or (self.currentPage == 1 and x >= screenWidth * BOARD_LEFT) then
        return pointer(self, event, x, y, ...)
    end
    local notches = cursor.wheel or 0
    if self.pageChangeTo == nil and cursor.wheelId ~= self.wheelId and notches ~= 0 then
        self.wheelId = cursor.wheelId
        local page = notches < 0 and 1 or 0
        if page ~= self.currentPage then self.pageChangeTo = page end
    end
    cursor.wheel = 0  -- not for the leaderboard
    local results = {pointer(self, event, x, y, ...)}
    cursor.wheel = notches
    return unpack(results)
end
)";

}  // namespace

SLINGSHOT_MOUSE_WHEEL_MOD(kAdapter, {{"data/scripts/ui_components/ScrollFrame2.lua",
                                      slingshot::mods::mouse_wheel::kScrollFrameLua},
                                     {"data/scripts/menus/tournament/seasons_tournament_menu.lua", kTournamentMenu}});
