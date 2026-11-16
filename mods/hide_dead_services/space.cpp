// SPDX-License-Identifier: GPL-3.0-or-later
// Hides menu entries of discontinued online services, which only open web pages
// that are gone (Rovio's redirects no longer lead anywhere). Pictures that are
// part of the scenery stay but no longer react.
#include <string>

#include "common/game_scripts.hpp"
#include "common/hide.hpp"
#include "mod.hpp"

namespace {

// The game's switches (buildSettings and channelEnabled, read when menus and
// level selection scenes are built). Off, they leave out the full story video,
// the Facebook and Twitter bubbles, the Star Wars and Angry Birds Friends store
// links, the rovers' and asteroid miners' info buttons (NASA pages), the Ocean
// Elders button and ToonsTV.
constexpr const char* kGame = R"(
local initialize = initializeGame
function initializeGame(...)
    local results = {initialize(...)}
    for _, link in ipairs({'trailerLink', 'socialLinks', 'nasaLinks', 'oceanEldersLink', 'starWarsCrossSell', 'abFriendsCrossSell'}) do
        buildSettings[link] = false
    end
    channelEnabled = false
    return unpack(results)
end
)";

// Episode selection: the scene (scenes/EpisodeSelection.lua) has its own
// Facebook, Twitter, Star Wars and Angry Birds Friends icons beside the planets.
constexpr const char* kEpisodeSelection = R"(
local dead = {'facebook_link', 'twitter_link', 'starwars_link', 'friends_link'}
local load = native.loadLuaTable
function native.loadLuaTable(path, ...)
    local scene, err = load(path, ...)
    if path == 'scenes/EpisodeSelection.lua' and type(scene) == 'table' and scene.entities then
        for _, name in ipairs(dead) do scene.entities[name] = nil end
    end
    return scene, err
end
)";

// The ISS, Curiosity, Apollo and Europa beside the planets and the Kellogg's
// logo in its episode are scenery that opened web pages.
constexpr const char* kUrl = R"(
local inert = {NASA_URL = true, CURIOSITY_URL = true, BUZZ_APOLLO_URL = true, EUROPA_URL = true, KELLOGGSPROMO_URL = true}
local prompt = url.prompt
function url.prompt(key, ...)
    if inert[key] then return end
    return prompt(key, ...)
end
)";

// Main menu: the options slider's notifications (nothing sends them) and code
// entry (codes are redeemed on Rovio's payment server). The slider stacks all
// its children, so they are taken out rather than hidden.
constexpr const char* kMainMenu = R"(
local dead = {notificationButton = true, showKeyboardButton = true}
local create = menu.MainMenu.createOptionsSliderButtons
function menu.MainMenu:createOptionsSliderButtons(slider, ...)
    local results = {create(self, slider, ...)}
    for i = #slider.children, 1, -1 do
        local child = slider.children[i]
        if dead[child.name] then slider:removeChild(child) end
    end
    return unpack(results)
end
)";

// New Horizons: the results stay, "Learn more" (a web page) goes.
constexpr const char* kPluto = R"(
hide_buttons(ui.PlutoVotePopup, {learnMore = true, learnMoreText = true})
)";

class HideDeadServices final : public slingshot::Mod {
public:
    HideDeadServices() {
        using slingshot::mods::append_lua;
        append_lua("data/scripts/game.lua", kGame);
        append_lua("data/scripts/menu/EpisodeSelection.lua", kEpisodeSelection);
        append_lua("data/scripts/url.lua", kUrl);
        append_lua("data/scripts/menu/MainMenu.lua", kMainMenu);
        append_lua("data/scripts/menu/PlutoVotePopup.lua", std::string(slingshot::mods::kHideLua) + kPluto);
    }
};

}  // namespace

SLINGSHOT_MOD(HideDeadServices, "hide-dead-services",
              "Hide menu buttons of discontinued online services (ToonsTV, Facebook, Twitter, NASA pages, store links, codes, ...)");
