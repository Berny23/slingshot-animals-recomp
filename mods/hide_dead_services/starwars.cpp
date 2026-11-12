// SPDX-License-Identifier: GPL-3.0-or-later
// Hides the menu entries that only open discontinued web pages: the full story
// video in the main menu's arrow menu (a redirect that no longer leads to it).
#include "common/game_scripts.hpp"
#include "mod.hpp"

namespace {

// The arrow menu places its buttons by their order in `children`, so the button is
// taken out rather than hidden. setMainmenuItemsVisible still sets its `visible`.
// Without Twitter it was the arrow menu's only button: then the arrow goes too,
// every frame, as setMainmenuItemsVisible shows it again.
constexpr const char* kMainMenu = R"(
local create = MainMenu.createRightOptionSliderButtons
function MainMenu:createRightOptionSliderButtons(slider, ...)
    local results = {create(self, slider, ...)}
    for i, child in ipairs(slider.children) do
        if child.name == 'trailerButton' then
            table.remove(slider.children, i)
            break
        end
    end
    self.rightSliderEmpty = #slider.children == 0
    return unpack(results)
end
local update = MainMenu.update
function MainMenu:update(...)
    local results = {update(self, ...)}
    if self.rightSliderEmpty then
        for _, name in ipairs({'rightSlider', 'rightSliderButton'}) do
            local child = self:getChild(name)
            if child then child.visible = false end
        end
    end
    return unpack(results)
end
local get_child = MainMenu.getChild
local removed = {}
function MainMenu:getChild(name, ...)
    local child = get_child(self, name, ...)
    if child == nil and name == 'trailerButton' then return removed end
    return child
end
)";

class HideDeadServices final : public slingshot::Mod {
public:
    HideDeadServices() { slingshot::mods::append_lua("data/scripts/menus/main_menu.lua", kMainMenu); }
};

}  // namespace

SLINGSHOT_MOD(HideDeadServices, "hide-dead-services", "Hide the full story video, whose web page is gone");
