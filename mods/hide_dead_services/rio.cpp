// SPDX-License-Identifier: GPL-3.0-or-later
// Hides menu entries of discontinued online services, which only show connection
// errors or web pages that are gone. Video and cross-promotion buttons show only
// with a ready ad, which never comes.
#include "common/game_scripts.hpp"
#include "mod.hpp"

namespace {

// The game's switches (options.lua):
//   channelEnabled              ToonsTV in the main menu and the pause menu
//   g_use_ads_offerwall         the coin shop's "Get Free Coins!" (an ad offer wall)
//   g_use_apprater              the level end's rating corner (a store page)
constexpr const char* kOptions = R"(
gamelua.channelEnabled = false
gamelua.g_use_ads_offerwall = false
gamelua.g_use_apprater = false
)";

// Settings: Facebook and Twitter, and notifications (nothing sends them). The
// popup leaves the social buttons out for players under 13 (the age gate); it
// is built as for them, and without notifications.
constexpr const char* kComponents = R"(
local make = makeSettingsPopup
function makeSettingsPopup(...)
    local g = gamelua
    local saved = {g.g_use_agegate, g.settings.ageGate, g.g_use_local_notifications, g.g_use_remote_notifications}
    g.g_use_agegate, g.settings.ageGate = true, {age = 0}
    g.g_use_local_notifications, g.g_use_remote_notifications = false, false
    local ok, popup = pcall(make, ...)
    g.g_use_agegate, g.settings.ageGate, g.g_use_local_notifications, g.g_use_remote_notifications = unpack(saved, 1, 4)
    if not ok then error(popup, 0) end
    return popup
end
)";

class HideDeadServices final : public slingshot::Mod {
public:
    HideDeadServices() {
        using slingshot::mods::append_lua;
        append_lua("data/scripts/options.lua", kOptions);
        append_lua("data/scripts/Components.lua", kComponents);
    }
};

}  // namespace

SLINGSHOT_MOD(HideDeadServices, "hide-dead-services",
              "Hide menu buttons of discontinued online services (ToonsTV, Facebook, Twitter, free coins, rating, ...)");
