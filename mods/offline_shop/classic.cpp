// SPDX-License-Identifier: GPL-3.0-or-later
// A shop without real money: no gem packs or gem "+", the Mighty Eagle costs
// coins, and levels pay coins and gems.
//
// The game's own star coins (reward_config.star_rewards: 1, 2, 3) stay, including
// the coin stacks on the level end screen. Newly earned stars pay the rest up to
// these amounts, scaled with the episode's position:
//
//     star             1st    2nd    3rd    level total
//     first episode     10     15     25     50
//     last episode      20     30     50    100
//
// A replay without new stars pays 6 to 12 coins × stars / 3, plus a gem for three
// stars. Each new star pays a gem, the first three-star result 2 more: about 3500
// gems for all episode levels (an extra bird costs 30-120, an egg 50).
#include <string>

#include "common/game_scripts.hpp"
#include "mod.hpp"

namespace {

constexpr const char* kSettings = R"(
local EAGLE_PRICE = 1500           -- coins
local STAR_REWARDS = {10, 15, 25}  -- coins for the 1st/2nd/3rd star, first episode
local REPLAY_REWARD = 6            -- coins for three stars without a new one
local REPLAY_GEMS = 1              -- gems for that
local GEMS_PER_STAR = 1            -- for each new star
local THREE_STAR_GEMS = 2          -- on top, for the first three-star result
local LEAGUE_REPLAY_REWARD = 2     -- coins for three stars without a new one, Mighty League
)";

// Episode levels, without the Mighty League, Daily Challenge, golden eggs and
// hidden episodes (tutorials, power-up try-outs, Mighty Eagle demo).
constexpr const char* kRewards = R"(
-- 1 for the first episode up to 2 for the last.
local function difficulty(level)
    local _, episode = gamelua.getLevelById(level)
    local ids = gamelua.g_episodeIds or {}
    for i, id in ipairs(ids) do
        if id == episode and #ids > 1 then return 1 + (i - 1) / (#ids - 1) end
    end
    return 1
end
local function credit(items)
    -- The counter is updated by the level end's animations (kLevelEnd).
    local animated = {}
    for name in pairs(items) do animated[name] = true end
    gamelua.ingameCurrencySystem:addIGCItems(items, {
        type_of_event = gamelua.subsystems.IngameCurrencySystem.IGC_EVENT_TYPE_REWARD,
        from = 'episode level',
    }, animated)
end
local ended = MuseumLevelFlow.levelEnded
function MuseumLevelFlow.levelEnded(params, ...)
    local g = gamelua
    g.offlineShopStarRewards, g.offlineShopExtra = nil, nil
    local _, id = g.getLevelById(params.level)
    local data = g.g_episodes and g.g_episodes[id]
    local episode = params.levelComplete and not g.isLeagueMode() and not g.isDCMode()
        and g.getLevelType(params.level) ~= 'golden_egg' and data and not data.hidden
    if not episode then return ended(params, ...) end
    local entry = g.highscores and g.highscores[g.levelName]
    local claimed = entry and entry.claimedStars or 0
    local stars = g.g_get_star_count(params.level, params.score)
    local factor = difficulty(params.level)
    local full = {}
    for i, amount in ipairs(STAR_REWARDS) do full[i] = math.floor(amount * factor + 0.5) end
    g.offlineShopStarRewards = full  -- the new stars' coins, for kLevelEnd
    local paid = g.remoteConfigTable.reward_config.star_rewards or {}
    local results = {ended(params, ...)}
    local coins, extra = 0, {star = stars, coins = 0, gems = 0}
    for i = claimed + 1, math.min(stars, #STAR_REWARDS) do
        coins = coins + math.max(0, full[i] - (paid[i] or 0))
        extra.gems = extra.gems + GEMS_PER_STAR
    end
    if stars > 0 and stars <= claimed then
        extra.coins = math.floor(REPLAY_REWARD * factor * stars / 3 + 0.5)
        coins = extra.coins
        if stars >= 3 then extra.gems = REPLAY_GEMS end
    end
    if claimed < 3 and stars >= 3 then extra.gems = extra.gems + THREE_STAR_GEMS end
    g.offlineShopExtra = extra  -- what kLevelEnd animates besides the stars' coins
    local items = {}
    if coins > 0 then items.coins = coins end
    if extra.gems > 0 then items.gems = extra.gems end
    if next(items) then credit(items) end
    return unpack(results)
end
)";

// Mighty League levels pay no coins for stars. New stars pay as in the first
// episode; a three-star replay pays LEAGUE_REPLAY_REWARD coins.
constexpr const char* kLeagueRewards = R"(
local league_ended = MuseumLevelFlow.levelEnded
function MuseumLevelFlow.levelEnded(params, ...)
    local g = gamelua
    local level = g.isLeagueMode() and params.levelComplete and g.leagueModern and g.leagueModern.startedLevel
    if not level or not level.scoresToPass then return league_ended(params, ...) end
    local utils = g.leagueModernUtils
    local name = level.name or level.levelName
    local saved = g.highscores[name] and g.highscores[name].score or 0
    -- offline-league clears a round's levels; their stars are paid once all the same
    g.settings.offlineShopLeagueStars = g.settings.offlineShopLeagueStars or {}
    local paid = g.settings.offlineShopLeagueStars
    local before = math.max(utils:calculateStarsFromScores(math.max(level.highscore or 0, saved), level.scoresToPass) or 0,
                            paid[name] or 0)
    local stars = utils:calculateStarsFromScores(params.score, level.scoresToPass) or 0
    if stars > (paid[name] or 0) then
        paid[name] = stars
        g.settingsWrapper:saveSettings()
    end
    local results = {league_ended(params, ...)}
    local coins, gems = 0, 0
    for i = before + 1, math.min(stars, #STAR_REWARDS) do
        coins = coins + STAR_REWARDS[i]
        gems = gems + GEMS_PER_STAR
    end
    if before < 3 and stars >= 3 then gems = gems + THREE_STAR_GEMS end
    if stars >= 3 and before >= 3 then coins = LEAGUE_REPLAY_REWARD end
    if stars > 0 then g.offlineShopLeagueExtra = {star = stars, coins = coins, gems = gems} end
    local items = {}
    if coins > 0 then items.coins = coins end
    if gems > 0 then items.gems = gems end
    if next(items) then
        local animated = {}
        for name in pairs(items) do animated[name] = true end
        g.ingameCurrencySystem:addIGCItems(items, {
            type_of_event = g.subsystems.IngameCurrencySystem.IGC_EVENT_TYPE_REWARD,
            from = 'league level',
        }, animated)
    end
    return unpack(results)
end
)";

// Level end: each new star's coins fly in with the full amount (the drawn coin
// stacks stay original). The last star also sends a replay's coins and the gems.
constexpr const char* kLevelEnd = R"(
local achieved = LevelComplete.setStarAchieved
function LevelComplete:setStarAchieved(star, ...)
    local config = gamelua.remoteConfigTable.reward_config
    local original = config.star_rewards
    config.star_rewards = gamelua.offlineShopStarRewards or original
    local ok, a, b, c = pcall(achieved, self, star, ...)
    config.star_rewards = original
    if not ok then error(a, 0) end
    local extra = gamelua.offlineShopExtra
    local frame = self:getChild('star' .. tostring(star))
    if extra and extra.star == star and frame then
        gamelua.offlineShopExtra = nil
        local currency = gamelua.ingameCurrencySystem
        if extra.coins > 0 then currency:addCoinsAnimation(extra.coins, frame.x, frame.y) end
        if extra.gems > 0 then currency:addGemsAnimation(extra.gems, frame.x, frame.y) end
    end
    return a, b, c
end
)";

// League level end: kLeagueRewards' coins and gems fly from the last star.
constexpr const char* kLeagueLevelEnd = R"(
local achieved = LeagueLevelEnd.setStarAchieved
function LeagueLevelEnd:setStarAchieved(star, ...)
    local results = {achieved(self, star, ...)}
    local extra = gamelua.offlineShopLeagueExtra
    local frame = self:getChild('star' .. tostring(star))
    if extra and extra.star == star and frame then
        gamelua.offlineShopLeagueExtra = nil
        local currency = gamelua.ingameCurrencySystem
        if extra.coins > 0 then currency:addCoinsAnimation(extra.coins, frame.x, frame.y) end
        if extra.gems > 0 then currency:addGemsAnimation(extra.gems, frame.x, frame.y) end
    end
    return unpack(results)
end
)";

// Coins and gems fly in 0.15 s apart, so 100 coins took 15 s. More than
// ANIMATED_PACE items take as long as ANIMATED_PACE.
constexpr const char* kAnimationSpeed = R"(
local ANIMATED_PACE = 10
local INTERVAL = 0.15  -- the game's
local function faster(animate)
    return function(self, amount, x, y, interval, ...)
        if interval == nil and amount and amount > ANIMATED_PACE then
            interval = INTERVAL * ANIMATED_PACE / amount
        end
        return animate(self, amount, x, y, interval, ...)
    end
end
IngameCurrencySystem.addCoinsAnimation = faster(IngameCurrencySystem.addCoinsAnimation)
IngameCurrencySystem.addGemsAnimation = faster(IngameCurrencySystem.addGemsAnimation)
)";

// The currency popup keeps only its coin tab; every way into the gem tab leads there.
constexpr const char* kShop = R"(
local function drop_gem_tab(layouts)
    local popup = layouts and layouts.layout_currency_popup
    if popup and popup.tabs and popup.tabs.tab_gems then
        popup.tabs.tab_gems = nil
        for _, tab in pairs(popup.tabs) do tab.tabIndex = 1 end
    end
end
local open = shop.requestOpenCurrencyPopup
function shop.requestOpenCurrencyPopup(tab, ...)
    drop_gem_tab(gamelua.remoteConfigTable.shopLayouts)
    drop_gem_tab(gamelua.ShopLayoutTable)
    return open('tab_coins', ...)
end
)";

// The "not enough gems" popup loses its button into the gem packs.
constexpr const char* kOutOfGems = R"(
local init = ui.ShopModernOutOfPopup.init
function ui.ShopModernOutOfPopup:init(params, ...)
    local results = {init(self, params, ...)}
    local button = self:getChild('acceptButton')
    if button and self.targetTab == gamelua.IGCItemInfo.gems.shopTab then self:removeChild(button) end
    return unpack(results)
end
)";

// The currency display: no "+" on gems, and touching them does nothing.
constexpr const char* kCurrencyItem = R"(
local function clear_returns(frame, value)
    if frame.returnValue == value then frame.returnValue = nil end
    for _, child in ipairs(frame.children or {}) do clear_returns(child, value) end
end
local init = IngameCurrencyItem.init
function IngameCurrencyItem:init(args, ...)
    if args and args.name == 'gems' then args.hidePlus = true end
    local results = {init(self, args, ...)}
    if args and args.name == 'gems' then clear_returns(self, 'ADD_CURRENCY_POPUP') end
    return unpack(results)
end
)";

// The Mighty Eagle as a coin item, unlocked by the purchase's own code (Payment._onItemReceived).
constexpr const char* kEagleCatalog = R"(
local function add_eagle_to_catalog()
    local catalog = gamelua.remoteConfigTable and gamelua.remoteConfigTable.shopCatalog
    if catalog and not catalog.mighty_eagle2 then
        catalog.mighty_eagle2 = {quantity = {}, price = {type = 'coins', amount = EAGLE_PRICE}}
    end
end
)";

constexpr const char* kEagleBuy = R"(
local buy = IngameCurrencySystem.buyIGCItem
function IngameCurrencySystem:buyIGCItem(item, success, fail, ...)
    if not Payment.isLegacyMightyEagle(item) then return buy(self, item, success, fail, ...) end
    add_eagle_to_catalog()
    local function bought(...)
        Payment._onItemReceived(item)
        if success then return success(...) end
    end
    -- The "not enough coins" popup, as shop.lua shows it for other items.
    local function too_expensive()
        self:pushIGCFrame()
        local texts = shop.igcTypeOutOfTexts.coins
        local popup = gamelua.ui.ShopModernOutOfPopup:new({
            name = 'ShopModernConfirmPopup_mighty_eagle2',
            targetTab = 'tab_coins',
            exitCallback = function(tab)
                gamelua.notificationsFrame:removeChild(shop.confirmPopup)
                gamelua.ingameCurrencySystem:popIGCFrame()
                shop.confirmPopup = nil
                if not tab then return end
                -- The coin shop opens below the Mighty Eagle popup: close that one.
                for _, child in ipairs(gamelua.notificationsFrame.children or {}) do
                    if child.name == 'ShopModernMightyEaglePopup' and child.exit then child:exit() end
                end
                shop.requestOpenCurrencyPopup(tab, nil, shop.OPEN_FROM_OUTOF_POPUP)
            end,
            titleText = texts.title,
            contentText = texts.content,
            buttonText = texts.button,
            buttonImage = texts.buttonImg,
            missingAmount = math.abs(self:getIGCValue('coins') - EAGLE_PRICE),
        })
        gamelua.notificationsFrame:addChild(popup)
        popup:transitionIn()
        shop.confirmPopup = popup
    end
    return buy(self, 'mighty_eagle2', bought, too_expensive, ...)
end
)";

constexpr const char* kEagleButton = R"(
-- The price with a coin icon, as ShopModernProductBase shows it.
function ui.ShopModernMightyEagleButton:updatePrice(...)
    self:setPrice(true, EAGLE_PRICE)
    local text, button = self:getChild('price'), self:getChild('priceButton')
    if not text or not button then return end
    if not self.coinIcon then
        self.coinIcon = ui.Image:new()
        self.coinIcon:setImage(gamelua.IGCItemInfo.coins.iconId)
        self.coinIcon:scaleToSize(button.h * 0.5, button.h * 0.5)
        if UIGlimmer then
            self.coinIcon:addChild(UIGlimmer:new(UIGlimmer.priceTwinkle('PARTICLE_GOLD', self.coinIcon)))
        end
        self:addChild(self.coinIcon)
    end
    text:setText(tostring(EAGLE_PRICE))
    local gap = 5
    local icon_w = self.coinIcon.w * self.coinIcon.scaleX
    local total = icon_w + gap + text.w * text.scaleX
    self.coinIcon.refX = button.refX - total / 2 + icon_w / 2
    self.coinIcon.refY = text.refY
    text.refX = self.coinIcon.refX + icon_w / 2 + gap + text.w * text.scaleX / 2
    self.coinIcon:setVisible(text.visible ~= false)
end
)";

// No "restore purchases" button.
constexpr const char* kEaglePopup = R"(
function ui.ShopModernMightyEaglePopup:updateRestoreButtonVisibility()
    local restore = self:getChild('bgImg', 'restore')
    if restore then restore:setVisible(false) end
end
)";

class OfflineShop final : public slingshot::Mod {
public:
    OfflineShop() {
        using slingshot::mods::append_lua;
        const std::string settings = kSettings;
        append_lua("data/scripts/gamefeatures/progress/MuseumLevelFlow.lua", settings + kRewards + kLeagueRewards);
        append_lua("data/scripts/menus/level_end.lua", std::string(kLevelEnd) + kLeagueLevelEnd);
        append_lua("data/scripts/menus/shop/shop.lua", kShop);
        append_lua("data/scripts/menus/shop/ShopModernOutOfPopup.lua", kOutOfGems);
        append_lua("data/scripts/IngameCurrency/IngameCurrencyItem.lua", kCurrencyItem);
        append_lua("data/scripts/IngameCurrency/IngameCurrencySystem.lua", settings + kEagleCatalog + kEagleBuy + kAnimationSpeed);
        append_lua("data/scripts/menus/shop/ShopModernMightyEagleButton.lua", settings + kEagleButton);
        append_lua("data/scripts/menus/shop/ShopModernMightyEaglePopup.lua", kEaglePopup);
    }
};

SLINGSHOT_MOD(OfflineShop, "offline-shop",
              "The Mighty Eagle costs coins, levels pay more coins and gems; no gem packs");

}  // namespace
