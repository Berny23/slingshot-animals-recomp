// SPDX-License-Identifier: GPL-3.0-or-later
// Hides the menu entries of discontinued online services, which only show
// connection errors or wait for answers that never come. The ToonsTV billboard
// on the island is part of the scenery: it stays but no longer reacts.
#include <string>

#include "common/game_scripts.hpp"
#include "common/hide.hpp"
#include "mod.hpp"

namespace {

// The game's switches (options.lua):
//   g_use_zappar   the eye button on the island and chapter map (Zappar, an AR app)
//   g_use_ads      the video buttons of the power-up refill, the event timer
//                  and the reward wheel (the popups have a layout without them)
constexpr const char* kOptions = R"(
gamelua.g_use_zappar = false
gamelua.g_use_ads = false
)";

// The ToonsTV billboard (a ButtonElement of type ToonsTvButton): it shows a
// connection screen that never ends. Buttons take part in clicks through
// onUpdateBlock, which the billboard skips.
constexpr const char* kButtonElement = R"(
local update_block = ButtonElement.onUpdateBlock
function ButtonElement:onUpdateBlock(obj, def, ...)
    local element = def and def.components and def.components.button_element
    if element and element.type == 'ToonsTvButton' then return end
    return update_block(self, obj, def, ...)
end
)";

// Island: the Facebook log-in, and the bar of friends' scores (Facebook friends).
constexpr const char* kIslandMap = R"(
hide_buttons(ui.IslandMap, {btnRovioIdFacebookLogin = true, socialbar = true})
)";

// Chapter map: the Facebook log-in.
constexpr const char* kMapScreen = R"(
hide_buttons(ui.MapScreen, {btnRovioIdFacebookLogin = true})
)";

// Settings: Facebook connect.
constexpr const char* kSettings = R"(
local entry = ui.SettingsPopup.onEntry
function ui.SettingsPopup:onEntry(...)
    local results = {entry(self, ...)}
    hide_in(self, {btnRovioAccountSocialLogin = true})
    return unpack(results)
end
)";

// Level end: the bar of friends' scores with its Facebook connect, hidden as the
// game does on Windows Phone.
constexpr const char* kLevelCompleted = R"(
local init = ui.LevelCompleted.initSocialBar
function ui.LevelCompleted:initSocialBar(...)
    local results = {init(self, ...)}
    hide_in(self, {socialBar = true})
    return unpack(results)
end
)";

class HideDeadServices final : public slingshot::Mod {
public:
    HideDeadServices() {
        using slingshot::mods::append_lua;
        const std::string hide = slingshot::mods::kHideLua;
        append_lua("data/scripts/options.lua", kOptions);
        append_lua("data/scripts/BlockComponents/UIComponents/ButtonElement.lua", kButtonElement);
        append_lua("data/scripts/menus/island/IslandMap.lua", hide + kIslandMap);
        append_lua("data/scripts/menus/map_screen/MapScreen.lua", hide + kMapScreen);
        append_lua("data/scripts/menus/SettingsPopup.lua", hide + kSettings);
        append_lua("data/scripts/menus/level_end/LevelCompleted.lua", hide + kLevelCompleted);
    }
};

}  // namespace

SLINGSHOT_MOD(HideDeadServices, "hide-dead-services",
              "Hide menu buttons of discontinued online services (ToonsTV, Facebook, Zappar, video ads)");
