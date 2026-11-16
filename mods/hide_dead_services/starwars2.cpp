// SPDX-License-Identifier: GPL-3.0-or-later
// Hides the menu entries of discontinued online services, which only show
// connection errors or web pages that are gone. The level end's video button
// shows only with a ready ad, which never comes. The account panel that slides
// in at the start ("You're playing as: Guest") is not shown.
#include <string>

#include "common/game_scripts.hpp"
#include "common/hide.hpp"
#include "mod.hpp"

namespace {

// Main menu. The middle row: ToonsTV and the Rovio Account log-in. layout
// places (and shows) the buttons listed in `mid_buttons`, so they are taken
// out of the list first. The options slider: the Jenga code entry (codes are
// checked on Rovio's server); the slider places its buttons by their order in
// `children`, so it is taken out rather than hidden. The Rovio Account panel
// with the account name, shown after the log-in, is left out.
constexpr const char* kMainMenu = R"(
local function no_account_panel()
    local game = gamelua or {}
    for _, account in pairs({RovioAccount or false, game.RovioAccount or false}) do
        if account then account.showIDPopup = function() end end
    end
end
no_account_panel()
local dead = {channelButton = true, rovioLoginButton = true}
local layout = MainMenu.layout
function MainMenu:layout(...)
    no_account_panel()
    local buttons = self.mid_buttons
    for i = #(buttons or {}), 1, -1 do
        if dead[buttons[i]] then table.remove(buttons, i) end
    end
    return layout(self, ...)
end
hide_buttons(MainMenu, dead)
local create = MainMenu.createOptionsSliderButtons
function MainMenu:createOptionsSliderButtons(slider, ...)
    local results = {create(self, slider, ...)}
    for i = #slider.children, 1, -1 do
        if slider.children[i].name == 'showKeyboardButton' then table.remove(slider.children, i) end
    end
    return unpack(results)
end
)";

class HideDeadServices final : public slingshot::Mod {
public:
    HideDeadServices() {
        slingshot::mods::append_lua("data/scripts/menus/main_menu.lua",
                                    std::string(slingshot::mods::kHideLua) + kMainMenu);
    }
};

}  // namespace

SLINGSHOT_MOD(HideDeadServices, "hide-dead-services",
              "Hide menu buttons and messages of discontinued online services (ToonsTV, Rovio Account, Jenga codes)");
