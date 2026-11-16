// SPDX-License-Identifier: GPL-3.0-or-later
// Hides menu entries of discontinued online services, which only show connection
// errors, empty pages or web pages that are gone. The level end's video button,
// the Sling Scope offer and the ad-removal page show only with a ready ad, which
// never comes.
#include <string>

#include "common/game_scripts.hpp"
#include "common/hide.hpp"
#include "mod.hpp"

namespace {

// Main menu: ToonsTV and Facebook log-in. The game's own disableSocialLinks
// drops the arrow menu at the bottom right (YouTube trailer, Twitter). The
// options slider stacks all its children, so push notifications (nothing sends
// them) is taken out rather than hidden.
constexpr const char* kMainMenu = R"(
disableSocialLinks = true
hide_buttons(MainMenu, {channelButton = true, facebookButton = true})
local create = MainMenu.createOptionsSliderButtons
function MainMenu:createOptionsSliderButtons(slider, ...)
    local results = {create(self, slider, ...)}
    for i = #slider.children, 1, -1 do
        local child = slider.children[i]
        if child.name == 'push_notification_button' then slider:removeChild(child) end
    end
    return unpack(results)
end
)";

// Main menu: the code entry (codes are redeemed on Rovio's payment server),
// the game's switch (options.lua).
constexpr const char* kOptions = R"(
gamelua.g_keyboard_button_on_main_menu_enabled = false
)";

// Level selection: the Facebook, Twitter and QZone buttons of the social pages
// (whose levels are open anyway), the video links of the golden egg pages (Rovio
// redirects) and the restore purchases button of the paid pages.
constexpr const char* kLevelSelection = R"(
local dead = {linkButton = true, restore_button = true}
for i = 1, 10 do
    dead['fbButton' .. i] = true
    dead['qzButton' .. i] = true
end
hide_buttons(SeasonsLevelSelection, dead)
)";

// Episode selection: the Angry Birds 2 banner in front of the episodes (a store page).
constexpr const char* kEpisodeExtras = R"(
local pages = gamelua.g_extra_pages
for i = #(pages and pages.pre or {}), 1, -1 do
    if pages.pre[i].name == 'AB2Button' then table.remove(pages.pre, i) end
end
)";

// The menu bar's News tab (Rovio's games and fan shop, from the server). The bar
// places the buttons in its list `buttons`.
constexpr const char* kButtonBar = R"(
local create = CombinedMenuButtonBar.createButtons
function CombinedMenuButtonBar:createButtons(...)
    local results = {create(self, ...)}
    for i = #(self.buttons or {}), 1, -1 do
        local button = self.buttons[i]
        if button.name == 'RovioNewsButton' then
            table.remove(self.buttons, i)
            self:removeChild(button)
        end
    end
    return unpack(results)
end
)";

// Quests: a Seek and Destroy quest showed a video button, which the hint
// replaced after a watched video: which level holds the object, with a "Go to
// Level" button. The quest screens read questVideoWatched from the quest's
// state, which is set here from the start.
constexpr const char* kQuestManager = R"(
local by_index = QuestManager.getActiveQuestByIndex
function QuestManager:getActiveQuestByIndex(...)
    local quest, state, index = by_index(self, ...)
    if quest and state and quest.questType == 'SeekAndDestroy' then state.questVideoWatched = true end
    return quest, state, index
end
)";

// Hats: the avatar, which only Facebook could change.
constexpr const char* kHats = R"(
hide_buttons(HatJournalFrame, {avatarBackground = true})
)";

// Mighty Eagle in the shop: the trailer (a web video) and restore purchases.
constexpr const char* kEagle = R"(
hide_buttons(CombinedShopGroupItemEagle, {trailer_button = true, restore_button = true})
)";

// The shop windows of Mighty Eagle and paid episodes and pages (shop.<name>,
// one per script): the trailer and restore purchases.
std::string shop_window(const std::string& name) {
    return "hide_buttons(shop." + name + ", {trailer_button = true, restore_button = true})\n";
}

class HideDeadServices final : public slingshot::Mod {
public:
    HideDeadServices() {
        using slingshot::mods::append_lua;
        const std::string hide = slingshot::mods::kHideLua;
        append_lua("data/scripts/options.lua", kOptions);
        append_lua("data/scripts/menus/main_menu.lua", hide + kMainMenu);
        append_lua("data/scripts/menus/seasons_level_selection.lua", hide + kLevelSelection);
        append_lua("data/scripts/menus/episode_selection_extra.lua", kEpisodeExtras);
        append_lua("data/scripts/menus/combinedMenu/CombinedMenuButtonBar.lua", kButtonBar);
        append_lua("data/scripts/quests/QuestManager.lua", kQuestManager);
        append_lua("data/scripts/menus/combinedMenu/HatJournalFrame.lua", hide + kHats);
        append_lua("data/scripts/menus/combinedMenu/CombinedShopGroupItemEagle.lua", hide + kEagle);
        for (const char* window : {"EagleWindow", "NBAWindow", "UnlockWindow"})
            append_lua(std::string("data/scripts/menus/shop/") + window + ".lua", hide + shop_window(window));
    }
};

}  // namespace

SLINGSHOT_MOD(HideDeadServices, "hide-dead-services",
              "Hide menu buttons of discontinued online services (ToonsTV, Facebook, Twitter, news, web videos, codes, ...)");
