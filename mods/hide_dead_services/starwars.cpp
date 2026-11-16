// SPDX-License-Identifier: GPL-3.0-or-later
// Hides the menu entries of discontinued online services, which only show
// connection errors or web pages that are gone (Rovio's redirects no longer
// lead anywhere).
#include <string>

#include "common/game_scripts.hpp"
#include "common/hide.hpp"
#include "mod.hpp"

namespace {

// ToonsTV in the main menu and episode selection: the game's switch
// (options.lua), read when the menus are built.
constexpr const char* kOptions = R"(
gamelua.channelEnabled = false
)";

// Main menu. The options slider: push notifications (nothing sends them) and
// the Telepod toy code entry (codes are checked on Rovio's server). The arrow
// menu: the full story video. Both sliders place their buttons by their order
// in `children`, so the buttons are taken out rather than hidden.
// setMainmenuItemsVisible still sets the video button's `visible`. Without
// Twitter it was the arrow menu's only button: then the arrow goes too, every
// frame, as setMainmenuItemsVisible shows it again.
constexpr const char* kMainMenu = R"(
local function take_out(slider, names)
    for i = #slider.children, 1, -1 do
        if names[slider.children[i].name] then table.remove(slider.children, i) end
    end
end
local create_options = MainMenu.createOptionsSliderButtons
function MainMenu:createOptionsSliderButtons(slider, ...)
    local results = {create_options(self, slider, ...)}
    take_out(slider, {notificationButton = true, showKeyboardButton = true})
    return unpack(results)
end
local create_right = MainMenu.createRightOptionSliderButtons
function MainMenu:createRightOptionSliderButtons(slider, ...)
    local results = {create_right(self, slider, ...)}
    take_out(slider, {trailerButton = true})
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

// Shop: the banner of Angry Birds' official shop (a store page).
constexpr const char* kShop = R"(
hide_buttons(Shop, {toyButton = true})
)";

class HideDeadServices final : public slingshot::Mod {
public:
    HideDeadServices() {
        using slingshot::mods::append_lua;
        append_lua("data/scripts/options.lua", kOptions);
        append_lua("data/scripts/menus/main_menu.lua", kMainMenu);
        append_lua("data/scripts/shop/Shop.lua", std::string(slingshot::mods::kHideLua) + kShop);
    }
};

}  // namespace

SLINGSHOT_MOD(HideDeadServices, "hide-dead-services",
              "Hide menu buttons of discontinued online services (ToonsTV, toy codes, notifications, web videos, ...)");
