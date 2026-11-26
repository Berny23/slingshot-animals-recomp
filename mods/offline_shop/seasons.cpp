// SPDX-License-Identifier: GPL-3.0-or-later
// A shop without real money: no coin packs, the items sold for money cost
// coins, and levels pay more coins.
//
//   Mighty Eagle                          1500 coins
//   Ham Dunk team levels                  1000
//   Tea Party levels                       500
//   Ragnahog calendar                     1000 (only before a level's date)
//   each of 12 hats                        250
//
// The 12 hats came only with bundles of Rovio's special offers (the product's
// unlockParams.hatQuests), which are not in the APK. The shop has them in a
// group of their own, and a tap on one in the hat journal buys it.
//
// The 19 episodes sold for coins cost 15 per level, 6840 in all. A new star N
// paid N coins (6 for three stars), and a video tripled it:
//
//   a new star          4, 6, 10 coins
//   a replay            1 coin per star
//
// 944 levels pay about 18,900 coins when all are 3-starred: enough for every
// episode and item above, with power-ups on top.
#include <string>

#include "common/game_scripts.hpp"
#include "mod.hpp"

namespace {

constexpr const char* kPrices = R"(
local PRICES = {
    mightyeagle = 1500,
    unlocknbalevels = 1000,
    unlockteapartylevels = 500,
    unlockragnahoglevels = 1000,
}
local HAT_PRICE = 250
-- The bundles' hats: the quest that gives each, the hat's id and its sprite.
local HATS = {
    {quest = 'RedBird_Pig', id = 'red_pig', sprite = 'HAT_RED_PIG'},
    {quest = 'BasicBird2_Justiina', id = 'white_justiina', sprite = 'HAT_WHITE_JUSTIINA'},
    {quest = 'BasicBird2_Chaplin', id = 'white_chaplin', sprite = 'HAT_WHITE_CHAPLIN'},
    {quest = 'BasicBird2_hammier', id = 'white_hockey', sprite = 'HAT_WHITE_JASON'},
    {quest = 'BombBird_Royalty', id = 'bomb_royalty', sprite = 'HAT_BOMB_ROYALTY'},
    {quest = 'BombBird_Shark', id = 'bomb_shark', sprite = 'HAT_BOMB_SHARK'},
    {quest = 'BombBird_pixel', id = 'bomb_pixel', sprite = 'HAT_BOMB_PIXEL'},
    {quest = 'GlobeBird_Piggywood', id = 'orange_moviegoer', sprite = 'HAT_ORANGE_MOVIEGOER1'},
    {quest = 'RedBigBird_Nessie', id = 'bigred_nessie', sprite = 'HAT_BIGRED_NESSIE'},
    {quest = 'RedBigBird_Bigfoot', id = 'bigred_bigfoot', sprite = 'HAT_BIGRED_BIGFOOT'},
    {quest = 'RedBigBird_hammier', id = 'bigred_turkey', sprite = 'HAT_BIGRED_TURKEY'},
    {quest = 'RedBigBird_Ragnahog1', id = 'bigredball', sprite = 'HAT_BIGRED_XMASBALL'},
}
local function hat_product(hat) return 'offlineshophat_' .. hat.quest end
for _, hat in ipairs(HATS) do PRICES[hat_product(hat)] = HAT_PRICE end
-- The item, priced in coins, when coins buy it.
local function priced(id)
    local item = PRICES[id] and g_iap_item_info and g_iap_item_info[id]
    if item then item.price = {coins = PRICES[id]} end
    return item
end
)";

// The shop shows and charges price.coins.
constexpr const char* kIapConfig = R"(
-- A hat is delivered as the bundles delivered it (activateUnlockPurchase).
offlineShopHatQuests = {}  -- for game-statistics
for _, hat in ipairs(HATS) do
    offlineShopHatQuests[hat.quest] = true
    local id = hat_product(hat)
    g_iap_item_info[id] = {product = id, iapId = id, name = id, isNonConsumable = true,
                           unlockParams = {hatQuests = {hat.quest}}}
end
for id in pairs(PRICES) do priced(id) end
)";

// The shop's hat group: four hats a page, the ones not owned, drawn about as
// big as the shop's other items.
//
// The Ham Dunk team levels: the game hides their pages until they are bought,
// and its shop group for them (unlocknba) is no longer in getShopItemGroups and
// has no card in the shop of this version. They get a group with a card of
// their own, for the levels only (unlocknbalevels), which stays after the
// purchase and says where the levels are.
constexpr const char* kShopConfig = R"(
local HAT_ICON_SIZE = 140
local groups = getShopItemGroups
function getShopItemGroups(...)
    local result = groups(...)
    result[#result + 1] = {name = 'offlineShopNba', topbar_icon = 'SHOP_BASKETBALL_MAIN_TOP', event = 'GOTO_MAIN_TAB',
                           pages = {{name = 'unlocknba', items = {{id = 'unlocknbalevels', background = 'SHOP_ELEMENT_2',
                                                                   icon = 'REWARD_NBA', iconScale = 0.75}},
                                     item_type = 'permanent', show_item_amount = false,
                                     topbar_icon = 'SHOP_BASKETBALL_MAIN_TOP'}}}
    local pages, page = {}, nil
    for _, hat in ipairs(HATS) do
        if not settingsWrapper:isCompletedQuest(hat.quest) then
            if not page or #page.items == 4 then
                page = {name = 'offlineShopHats' .. (#pages + 1), items = {}, item_type = 'permanent',
                        show_item_amount = false, topbar_icon = hat.sprite}
                pages[#pages + 1] = page
            end
            local w, h = res.getSpriteBounds(hat.sprite)
            local size = math.max(w or 0, h or 0)
            page.items[#page.items + 1] = {id = hat_product(hat), background = 'SHOP_ELEMENT_2', icon = hat.sprite,
                                           iconScale = size > 0 and math.min(2.5, HAT_ICON_SIZE / size) or 1}
        end
    end
    if #pages > 0 then
        result[#result + 1] = {name = 'offlineShopHats', topbar_icon = 'HAT_BOMB_ROYALTY',
                               event = 'GOTO_MAIN_TAB', pages = pages}
    end
    return result
end
)";

// A hat's or the team levels' card once owned: the shop's check instead of the
// price. The team levels' card has a title above its ball, which sits lower than
// the game places icons (a quarter of the card up, where it would touch the top).
constexpr const char* kShopItem = R"(
local NBA_TITLE = 'NBA Levels'  -- the game's texts are English only
local NBA_INFO = 'Unlocks the 30 team levels in Ham Dunk (2014)'
local OWNED = {
    unlocknbalevels = function() return settingsWrapper:isNBALevelsBought() end,
}
for _, hat in ipairs(HATS) do
    OWNED[hat_product(hat)] = function() return settingsWrapper:isCompletedQuest(hat.quest) end
end
local create = CombinedShopGroupItem.createElements
function CombinedShopGroupItem:createElements(item, ...)
    local results = {create(self, item, ...)}
    if item and item.id == 'unlocknbalevels' then
        local title = ui.Text:new()
        title.name = 'offlineShopTitle'
        title:setFont(fonts.getFont('fontHeader'))  -- fontMenu has only digits and x
        title.text = NBA_TITLE
        self:addChild(title)
        local info = ui.Text:new()
        info.name = 'offlineShopInfo'
        info:setFont(fonts.getFont('fontHeader'))
        info.text = NBA_INFO
        self:addChild(info)
    end
    return unpack(results)
end
local layout = CombinedShopGroupItem.layout
function CombinedShopGroupItem:layout(...)
    local results = {layout(self, ...)}
    local title = self:getChild('offlineShopTitle')
    if title then
        local w, h = Scaling.getSpriteBoundsScaled(self.image)
        local card_w = res.getSpriteBounds(self.image)
        local scale = math.min(0.75, 0.85 * card_w / math.max(title:getWidth(), 1))
        title.scaleX, title.scaleY = scale, scale
        title.y = -0.36 * h
        local info = self:getChild('offlineShopInfo')
        if info then
            info.scaleX, info.scaleY = 0.6, 0.6
            info.y = 0.66 * h
        end
        local icon = self:getChild('icon')
        if icon then icon.y = -0.02 * h end
    end
    return unpack(results)
end
local update = CombinedShopGroupItem.update
function CombinedShopGroupItem:update(...)
    local results = {update(self, ...)}
    local owned = OWNED[self.name]
    if owned and not self.offlineShopOwned and owned() then
        self.offlineShopOwned = true
        local button = self:getChild('buyButton')
        if button then
            button.visible = false
            button.returnValue = nil
            local check = ui.Image:new()
            check.name = 'offlineShopOwned'
            check:setImage('SHOP_BOUGHT')
            self:addChild(check)
        end
    end
    -- The card may be laid out after this: the check follows the hidden button.
    local check = self.offlineShopOwned and self:getChild('offlineShopOwned')
    local button = check and self:getChild('buyButton')
    if button then check.x, check.y = button.x, button.y end
    return unpack(results)
end
)";

// The shop's top bars space their buttons by each button's width: the buttons
// of the hats and the team levels get the width of the others and a common size.
constexpr const char* kTopBar = R"(
local ICON_SIZE = 100  -- about the size of the group icons (SHOP_*_TOP)
local function added(button)  -- the groups this mod adds
    local name = button.name or ''
    return name:find('^offlineShopHats') or name:find('^offlineShopNba')
end
local layout = CombinedTopBar.layout
function CombinedTopBar:layout(...)
    local results = {layout(self, ...)}
    local buttons = self.buttons or {}
    local width
    for _, button in ipairs(buttons) do
        if not added(button) then width = button.w break end
    end
    width = width or ICON_SIZE
    local function w(button) return added(button) and width or button.w end
    local moved
    for i, button in ipairs(buttons) do
        if added(button) then
            local scale = math.min(1.2, ICON_SIZE / math.max(button.w or 0, button.h or 0, 1))
            button.scaleX, button.scaleY = scale, scale
            button.x = (-#buttons / 2 + 0.5 + i - 1) * width * self.scaleX * 1.1
            moved = true
        end
    end
    if moved then
        local selector = self:getChild('topBarSelector')
        local current = buttons[self.currentItem]
        if selector and current then selector.x = current.x end
        local first, last = buttons[1], buttons[#buttons]
        local scale = Scaling.getScaleNum()
        self:getChild('tabBg'):setTotalW(last.x + 0.5 * w(last) * scale - (first.x - 0.5 * w(first) * scale))
    end
    return unpack(results)
end
)";

// The hat journal: a hat of the bundles that is not owned buys on a tap.
constexpr const char* kHatJournal = R"(
local QUEST = {}
for _, hat in ipairs(HATS) do QUEST['hatImage_' .. hat.id] = hat end
local BUY_TEXT = 'Get this hat for %d coins?'  -- the game's texts are English only

local function buy(journal, hat)
    notificationsFrame:addChild(ui.ConfirmPrompt:new({
        content = string.format(BUY_TEXT, HAT_PRICE),
        confirm_call = function()
            IngameCurrencyState:purchaseWithIngameCurrency(hat_product(hat))
            if settingsWrapper:isCompletedQuest(hat.quest) then journal:layout() end
        end,
    }))
end
local layout = HatJournalFrame.layout
function HatJournalFrame:layout(...)
    local results = {layout(self, ...)}
    local frames = self:getChild('birdFrames')
    for _, bird_frame in ipairs(frames and frames.children or {}) do
        for _, frame in ipairs(bird_frame.children or {}) do
            local bird = frame.getChild and frame:getChild('birdImage')
            for _, image in ipairs(bird and bird.children or {}) do
                local hat = QUEST[image.name]
                if hat and not settingsWrapper:isCompletedQuest(hat.quest) then
                    frame.dismissClicks = false
                    frame.returnValue = 'OFFLINE_SHOP_BUY_HAT'
                    frame.meta = hat
                end
            end
        end
    end
    return unpack(results)
end
local pointer = HatJournalFrame.onPointerEvent
function HatJournalFrame:onPointerEvent(...)
    local results = {pointer(self, ...)}
    if results[1] == 'OFFLINE_SHOP_BUY_HAT' then buy(self, results[2]) end
    return unpack(results)
end
)";

// The other screens ask the store. It answers for the coin items; the rest
// stays as offline.
constexpr const char* kCloudPayment = R"(
local initialized = CloudPayment.isInitialized
CloudPayment.isInitialized = function() return true end
local get_price = CloudPayment.getPrice
CloudPayment.getPrice = function(id, ...)
    if priced(id) then return tostring(PRICES[id]) end
    if initialized() then return get_price(id, ...) end
end
local available = CloudPayment.isProductAvailableForPurchase
CloudPayment.isProductAvailableForPurchase = function(id, ...)
    if priced(id) then return true end
    if initialized() then return available(id, ...) end
    return false
end
local buy = CloudPayment.buyProduct
CloudPayment.buyProduct = function(id, ...)
    if priced(id) then
        if not IngameCurrencyState:purchaseWithIngameCurrency(id) then
            eventManager:notify({id = events.EID_HIDE_LOADING_PAGE})
            eventManager:notify({id = events.EID_IAP_CANCELLED_BY_USER, product = id})
        end
        return
    end
    if initialized() then return buy(id, ...) end
    eventManager:notify({id = events.EID_HIDE_LOADING_PAGE})
    eventManager:notify({id = events.EID_IAP_FAILED_OTHER, product = id})
end

)";

constexpr const char* kCurrencyState = R"(
local TEXT = 'You need %d more coins.'  -- the game's texts are English only

-- A coin item is delivered as the store delivers it.
local handled = IngameCurrencyState.handlePurchasedItem
function IngameCurrencyState:handlePurchasedItem(product, suffix, ...)
    local results = {handled(self, product, suffix, ...)}
    local id = product .. (suffix or '')
    local item = priced(id)
    if item then
        eventManager:notify({id = events.EID_IAP_COMPLETED, status = 0, error = 0, product = id})
        if item.onPurchaseSuccessCallback then item.onPurchaseSuccessCallback(id, 0, 0) end
    end
    return unpack(results)
end

-- Not enough coins: the game offers coin packs, here a message says how many
-- are missing.
local missing
local purchase = IngameCurrencyState.purchaseWithIngameCurrency
function IngameCurrencyState:purchaseWithIngameCurrency(product, suffix, price, ...)
    local item = g_iap_item_info[product .. (suffix or '')]
    missing = (price or item and item.price and item.price.coins or 0) - (self.currencyAmount or 0)
    local results = {purchase(self, product, suffix, price, ...)}
    missing = nil
    return unpack(results)
end
function IngameCurrencyState:showIngameCurrencyPopup()
    -- The purchase shows the loading page; only a successful one hides it.
    eventManager:notify({id = events.EID_HIDE_LOADING_PAGE})
    if not missing or missing <= 0 then return end
    notificationsFrame:addChild(ui.MessageBox:new({content = string.format(TEXT, missing)}))
end
)";

// The coin counter's "+" (coin packs).
constexpr const char* kCurrencyBox = R"(
local init = IngameCurrencyBox.init
function IngameCurrencyBox:init(...)
    local results = {init(self, ...)}
    local plus = self:getChild('buyCurrency')
    if plus then
        plus.visible = false
        plus.returnValue = nil
    end
    return unpack(results)
end
)";

// The game pays its coins as they fly in; the rest follows when they are done.
constexpr const char* kLevelEnd = R"(
local STAR_COINS = {4, 6, 10}
local REPLAY_COINS = 1  -- per star

local function pay(self)
    local extra = self.offlineShopCoins
    self.offlineShopCoins = nil
    if extra and extra > 0 then IngameCurrencyState:addCurrency(extra) end
end
local entry = LevelComplete.onEntry
function LevelComplete:onEntry(...)
    local results = {entry(self, ...)}
    local stars, before = self.stars or 0, self.old_level_stars or 0
    local extra = 0
    for star = before + 1, math.min(stars, #STAR_COINS) do
        extra = extra + STAR_COINS[star] - star
    end
    if stars > 0 and stars <= before then extra = REPLAY_COINS * stars end
    self.offlineShopCoins = extra
    return unpack(results)
end
local update = LevelComplete.update
function LevelComplete:update(...)
    local results = {update(self, ...)}
    if self.offlineShopCoins and (self.currencyAdded or 0) >= (self.totalRewardCoins or 0) then pay(self) end
    return unpack(results)
end
local exit = LevelComplete.onExit
function LevelComplete:onExit(...)
    pay(self)
    return exit(self, ...)
end
)";

// The screens with the store's price text.
constexpr const char* kCoinIcons = R"(
-- A buy button whose 'price' shows one of these prices gets a coin in front,
-- placed as the shop places its coin prices (CombinedShopGroupItem).
local COIN_TEXTS = {}
for _, coins in pairs(PRICES) do COIN_TEXTS[tostring(coins)] = true end
local function game(name)  -- some screens reach the game's globals through gamelua
    local value = getfenv()[name]
    if value == nil and gamelua then value = gamelua[name] end
    if value == nil then value = _G[name] end
    return value
end
local function coin_price(button)
    local price = button and button.getChild and button:getChild('price')
    if not price then return end
    local coin = button:getChild('offlineShopCoin')
    if not (price.visible and COIN_TEXTS[tostring(price.text)]) then
        if coin then coin.visible = false end
        return
    end
    if not coin then
        coin = (game('Image') or game('ui').Image):new()
        coin.name = 'offlineShopCoin'
        coin:setImage('CURRENCY_COIN')
        button:addChild(coin)
    end
    local coin_w = coin.w or game('res').getSpriteBounds('CURRENCY_COIN')
    price:setFont(game('fonts').getFont('fontCoin'))
    price.hanchor = 'LEFT'
    local fit = 1
    if (button.w or 0) > 0 then
        fit = math.min(button.w * (button.scaleX or 1) / (coin_w + price:getWidth()), 1)
    end
    local scale = game('Scaling').getScaleNum()
    coin.x = -0.5 * price:getWidth() * (button.scaleX or 1) * fit * scale
    coin.y = 0
    coin.visible = true
    price.x = coin.x + 0.5 * coin_w * scale * fit
    price.y = 0
end

local function after(class, name, button)
    local original = class and class[name]
    if not original then return end
    class[name] = function(self, ...)
        local results = {original(self, ...)}
        coin_price(self:getChild(button))
        return unpack(results)
    end
end
)";
constexpr const char* kNbaPass = R"(
after(NBA_pass, 'onEntry', 'buyButton')
after(NBA_pass, 'refresh', 'buyButton')
)";
constexpr const char* kUnlockEpisode = R"(
after(UnlockEpisodePopup, '_setPrice', 'buyButton')
)";
constexpr const char* kCalendar = R"(
after(calendar and calendar.NoAccessPopup, '_setPrice', 'buyButton')
)";
constexpr const char* kGameHud = R"(
local update = GameHud.updateNBAPreview
function GameHud:updateNBAPreview(...)
    local results = {update(self, ...)}
    coin_price(self:getChild('previewBuyButton'))
    return unpack(results)
end
)";
// The shop's Mighty Eagle button has a coin image without a sprite. The card
// shows the bought Mighty Eagle only when created or after a restore: it
// switches when the Mighty Eagle is bought, as created for an owner (no
// background, the bought texts in its layout).
constexpr const char* kEagleItem = R"(
local create = CombinedShopGroupItemEagle.createElements
function CombinedShopGroupItemEagle:createElements(...)
    local results = {create(self, ...)}
    local button = self:getChild('buyButton')
    local coin = button and button:getChild('coin')
    if coin and not coin.image then coin:setImage('CURRENCY_COIN') end
    self.offlineShopOwned = settingsWrapper:isMightyEagleEnabled() == true
    return unpack(results)
end
local update = CombinedShopGroupItemEagle.update
function CombinedShopGroupItemEagle:update(...)
    local results = {update(self, ...)}
    local owned = settingsWrapper:isMightyEagleEnabled() == true
    if self.offlineShopOwned ~= nil and owned ~= self.offlineShopOwned then
        self.offlineShopOwned = owned
        self:setImage(owned and '' or 'SHOP_ME_BG')
        self:layout()
    end
    return unpack(results)
end
)";

// A shop opened for a group (the Mighty Eagle's and Mighty Ball's buttons ask
// for the Mighty Eagle) shows that group's page. The menu announces the group
// with an event that has no frame name, which the shop frame ignored, so the
// shop stayed on the page it was on before.
constexpr const char* kShopFrame = R"(
local triggered = CombinedShopFrame.eventTriggered
function CombinedShopFrame:eventTriggered(event, ...)
    if event and event.id == events.EID_SCROLLFRAME_ANCHOR_CHANGED and not event.name and event.itemName then
        for _, group in ipairs(self.groups or {}) do
            if group:hasPage(event.itemName) then
                local named = {name = 'combinedShopScrollFrame'}
                for k, v in pairs(event) do
                    if k ~= 'name' then named[k] = v end
                end
                return triggered(self, named, ...)
            end
        end
    end
    return triggered(self, event, ...)
end
)";

// The episode selection's Ham Dunk card counts the team levels' stars (and the
// coins, twice the stars, as the game does) only once they are bought; sold for
// coins, they count from the start, as in game-statistics.
constexpr const char* kEpisodeButton = R"(
local entry = EpisodeButton.onEntry
function EpisodeButton:onEntry(...)
    local results = {entry(self, ...)}
    local episode = self.episode and getLocalEpisode(self.episode)
    if episode and episode.name == 'Ham Dunk' and not settingsWrapper:isNBALevelsBought() then
        local extra = 0
        for _, page in ipairs(episode.pages or {}) do
            if page.free_levels == false and not page.ignoreInThreeStarCalculations then
                for _, level in ipairs(page.levels or {}) do
                    if not level.ignoreInThreeStarCalculations then extra = extra + 3 end
                end
            end
        end
        local stars, max, coins = calculateEpisodeStars(self.episode)
        if extra > 0 and stars and max then
            max = max + extra
            local function show(name, done, total)
                local text, indicator = self:getChild(name .. 'Text'), self:getChild(name .. 'Indicator')
                if text then text.text = done .. '/' .. total end
                if indicator then indicator:setProgress(done, total) end
            end
            show('star', stars, max)
            show('coin', coins or 0, max * 2)
        end
    end
    return unpack(results)
end
)";

class OfflineShop final : public slingshot::Mod {
public:
    OfflineShop() {
        using slingshot::mods::append_lua;
        const std::string prices = kPrices;
        append_lua("data/scripts/iap/iap_config.lua", prices + kIapConfig);
        append_lua("data/scripts/iap/shop_config.lua", prices + kShopConfig);
        append_lua("data/scripts/menus/combinedMenu/HatJournalFrame.lua", prices + kHatJournal);
        append_lua("data/scripts/menus/combinedMenu/CombinedTopBar.lua", kTopBar);
        append_lua("data/scripts/menus/combinedMenu/CombinedShopGroupItem.lua", prices + kShopItem);
        append_lua("data/scripts/iap_new.lua", prices + kCloudPayment);
        append_lua("data/scripts/ingameCurrency/ingameCurrencyState.lua", prices + kCurrencyState);
        append_lua("data/scripts/ingameCurrency/ingameCurrencyBox.lua", kCurrencyBox);
        append_lua("data/scripts/menus/level_end.lua", kLevelEnd);
        const std::string icons = prices + kCoinIcons;
        append_lua("data/scripts/menus/NBA_pass.lua", icons + kNbaPass);
        append_lua("data/scripts/menus/UnlockEpisodePopup.lua", icons + kUnlockEpisode);
        append_lua("data/scripts/menus/calendar/NoAccessPopup.lua", icons + kCalendar);
        append_lua("data/scripts/menus/GameHud.lua", icons + kGameHud);
        append_lua("data/scripts/menus/combinedMenu/CombinedShopGroupItemEagle.lua", kEagleItem);
        append_lua("data/scripts/menus/combinedMenu/CombinedShopFrame.lua", kShopFrame);
        append_lua("data/scripts/menus/EpisodeButton.lua", kEpisodeButton);
    }
};

}  // namespace

SLINGSHOT_MOD(OfflineShop, "offline-shop",
              "The Mighty Eagle, level packs and bundle hats cost coins, stars pay more; no coin packs");
