// SPDX-License-Identifier: GPL-3.0-or-later
// Hides menu entries of discontinued online services, which only show connection
// errors, empty web pages or endless spinners. The level end's ad button hides
// itself, as no ad is ever ready.
#include <string>

#include "common/game_scripts.hpp"
#include "common/hide.hpp"
#include "mod.hpp"

namespace {

// The buttons at the screen corners (names from data/scripts/menus/*.lua).
constexpr const char* kCornerButtons = R"(
local corner = {
    portfolioButton = true,      -- top left: Rovio's other games (a web page)
    rovioNewsFeedButton = true,  -- next to the settings button: Rovio news (a web page)
    channelButton = true,        -- Rovio channel (games, fan shop; web pages)
    facebookButton = true,
    twitterButton = true,
}
)";

// Main menu: the corner buttons, and the settings slider's Rovio Account, push
// notification and restore purchase entries. The slider stacks and shows all its
// children, so those are removed rather than hidden.
constexpr const char* kMainMenu = R"(
hide_buttons(MainMenu, corner)
local slider_dead = {
    rovio_login_button = true,
    push_notification_button = true,
    iap_restore_button = true,
}
-- The Daily Challenge card needs a download; Play and Mighty League move into its place.
local layout = MainMenu.layout
function MainMenu:layout(...)
    local results = {layout(self, ...)}
    local dc, play = self:getChild('dcButton'), self:getChild('playButton')
    local buttons = self:getChild('mainButtonsFrame')
    local league = buttons and buttons:getChild('leagueButton')
    if dc and play then
        -- (dc.scaleX may still be 0 here: the cards bounce in from scale 0.)
        local half = dc.w * 0.5 * Scaling.getScaleNum()
        play.refX = play.refX + half + 0.01 * screenWidth
        if league then
            league.refX = league.refX - half + 0.01 * screenWidth
            if self.leagueButtonSpinner then
                self.leagueButtonSpinner.x = league.x
                self.leagueButtonSpinner.y = league.y
            end
        end
    end
    return unpack(results)
end
corner.dcButton = true
local create = MainMenu.createOptionsSliderButtons
function MainMenu:createOptionsSliderButtons(slider, ...)
    local results = {create(self, slider, ...)}
    for i = #slider.children, 1, -1 do
        local child = slider.children[i]
        if slider_dead[child.name] then slider:removeChild(child) end
    end
    return unpack(results)
end
)";

// Shop: the row with the button for a free Sling Scope for watching an ad.
constexpr const char* kShop = R"(
local refresh = ui.ShopModernMain.refreshVideoButtonState
function ui.ShopModernMain:refreshVideoButtonState(...)
    local results = {refresh(self, ...)}
    if self.videoBg and self.videoBg.visible ~= false then
        self.videoBg:setVisible(false)
        if self.scopeButton then self.scopeButton.enabled = false end
    end
    return unpack(results)
end
)";

// The Sling Scope offer at a league level's start plays a reward video. The game
// checks for a ready video outside the Feather League only; this checks there too.
constexpr const char* kScopeOffer = R"(
local can_show = LeagueModernUtils.canShowScopePopup
function LeagueModernUtils:canShowScopePopup(...)
    return can_show(self, ...) and self:canWatchAdForScope()
end
)";

// The extra-bird offer: the video button goes, the gem button takes its place.
constexpr const char* kExtraBird = R"(
local init = ExtraBirdIngamePopup.init
function ExtraBirdIngamePopup:init(...)
    local results = {init(self, ...)}
    local ad, gems = self:getChild('adButton'), self:getChild('purchaseBirdBtn')
    if ad then
        self:removeChild(ad)
        if gems then gems.refY = ad.refY end
        self:layout()
    end
    return unpack(results)
end
)";

// Mighty League: the switch to the world leaderboard.
constexpr const char* kLeague = R"(
local function find_returning(frame, value)
    for _, child in ipairs(frame.children or {}) do
        if child.returnValue == value then return frame, child end
        local parent, found = find_returning(child, value)
        if found then return parent, found end
    end
end
local init = LeagueModernView.init
function LeagueModernView:init(...)
    local results = {init(self, ...)}
    local parent, switch = find_returning(self, 'TOGGLE_LEADERBOARD')
    if switch then parent:removeChild(switch) end
    return unpack(results)
end
function LeagueModernView:toggleLeaderboard() end
)";

// Trophy book: the world top 200 page goes, and so does the first trophy tab
// (league and medal trophies, all from the server) unless one is earned or
// offline-league is on (gamelua.offlineLeagueCount). Pages read their trophies by
// tab number, so the remaining pages keep their original one.
constexpr const char* kStickerBook = R"(
local function tab_earned(tab)
    local book = gamelua.stickerBook
    for _, sticker in pairs(book and book.Stickers or {}) do
        if (sticker.tab or 1) == tab and sticker.state ~= book.STICKER_STATE_LOCKED then return true end
    end
    return false
end
local create = StickerBookMenu.createPages
function StickerBookMenu:createPages(...)
    if not self.deadPagesRemoved then
        self.deadPagesRemoved = true
        local kept = {}
        for tab, entry in ipairs(self.menuStructure) do
            local page = entry.pageObject
            local dead = page == StickerBookLeaderboardPage
                or (page == StickerBookTrophyPage and not tab_earned(tab)
                    and not (tab == 1 and gamelua.offlineLeagueCount))
            if not dead then
                entry.pageObject = setmetatable({
                    new = function(_, args, ...)
                        args = args or {}
                        args.tabIndex = tab
                        return page:new(args, ...)
                    end,
                }, {__index = page})
                table.insert(kept, entry)
            end
        end
        self.menuStructure = kept
    end
    return create(self, ...)
end
)";

// Second-tab trophies that need the server are marked deprecated, which hides
// them until earned:
//   rage_quit      leave a league level from the pause menu (not in the Feather League)
//   super_star     first stars in 10 league levels (the Feather League has 9)
//   star_streak    18 new league stars a day on 2 days
//   social_bird    open a league while logged in to Facebook
//   bird_day_song  hear the Bird Day song in a league
//   collector      20 other trophies (9 can be earned offline)
constexpr const char* kStickers = R"(
for _, name in ipairs({'rage_quit', 'super_star', 'star_streak', 'social_bird', 'bird_day_song', 'collector'}) do
    local sticker = StickerBook.Stickers and StickerBook.Stickers[name]
    if sticker then sticker.deprecated = true end
end
)";

// The "Darude" trophy's button opens a web page; it grants the trophy instead
// (normally awarded by league music, which needs the server) and closes the popup.
constexpr const char* kLinks = R"(
local triggered = LinkListener.eventTriggered
function LinkListener:eventTriggered(event, ...)
    if event.id == gamelua.events.EID_GOTO_DARUDE_PAGE then
        if gamelua.stickerBook:getScore('darude') == 0 then gamelua.stickerBook:addScore('darude') end
        -- Reopening the popup shows the trophy as claimable.
        local popup = gamelua.notificationsFrame:getChild('stickerPopup')
        if popup and not popup.closing then
            popup.closing = true
            popup:close()
        end
        return
    end
    return triggered(self, event, ...)
end
)";

// The fan levels' postcard (Birdday 5 and its golden egg): its front was the fan's sketch,
// downloaded from Rovio's server, so it shows a download error. The postcard opens on its
// back (theme, description, the fan's name and country) and no longer flips.
constexpr const char* kSketch = R"(
local Element = ui.UGCLevelSketchElement
if Element and not Element.backOnly then
    Element.backOnly = true
    local init = Element.init
    function Element:init(...)
        init(self, ...)
        self.flipped = true
        if self.front then self.front:setVisible(false) end
        if self.back then self.back:setVisible(true) end
        if self.flipButton then self.flipButton:setVisible(false) end
    end
    function Element:flip() end
end
)";

class HideDeadServices final : public slingshot::Mod {
public:
    HideDeadServices() {
        using slingshot::mods::append_lua;
        const std::string prelude = std::string(slingshot::mods::kHideLua) + kCornerButtons;
        append_lua("data/scripts/menus/main_menu.lua", prelude + kMainMenu);
        append_lua("data/scripts/menus/episode_selection.lua", prelude + "hide_buttons(EpisodeSelection, corner)\n");
        append_lua("data/scripts/menus/level_selection.lua", prelude + "hide_buttons(LevelSelection, corner)\n");
        append_lua("data/scripts/menus/shop/ShopModernMain.lua", kShop);
        append_lua("data/scripts/league/LeagueModernUtils.lua", kScopeOffer);
        append_lua("data/scripts/menus/popups/ExtraBirdIngamePopup.lua", kExtraBird);
        append_lua("data/scripts/league/ui/LeagueModernView.lua", kLeague);
        append_lua("data/scripts/menus/StickerBookMenu.lua", kStickerBook);
        append_lua("data/scripts/subsystems/StickerDefinitions.lua", kStickers);
        append_lua("data/scripts/subsystems/LinkListener.lua", kLinks);
        append_lua("data/scripts/menus/popups/UGCLevelSketch.lua", kSketch);
    }
};

SLINGSHOT_MOD(HideDeadServices, "hide-dead-services",
              "Hide menu buttons of discontinued online services (news, more games, ads, Daily Challenge, world leaderboards, ...)");

}  // namespace
