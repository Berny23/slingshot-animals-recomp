// SPDX-License-Identifier: GPL-3.0-or-later
// Coins without real money. Rio sells coin packs and the Mighty Eagle; coins
// skip waits: an episode opens 24 h after the one before (or for 300 coins),
// Treasure Hunt levels open on timers (or for 10-120 coins), bonus levels with
// stars (or for coins). Here the Mighty Eagle costs EAGLE_PRICE coins, and
// level payouts rise so that coins cover it and the skips without replaying:
//
//   a new star        3, 5, 7 coins (the game: 0, 1, 2)
//   a new episode     300 coins (the game: 50)
//
// About 430 levels pay about 6400, the 14 episodes 4200: enough for the Mighty
// Eagle (1500), every episode wait (3600) and every Treasure Hunt timer
// (3570), with power-ups on top. Replays and Mighty Eagle results pay nothing,
// as in the game.
// Special offers paid with money or videos are taken out of the offer pool,
// and the coin shop (coin packs only) no longer opens.
#include <string>

#include "common/game_scripts.hpp"
#include "mod.hpp"

namespace {

// VariantConfig is the game's settings table (from variantconfig.json). The
// level end reads it when it pays.
constexpr const char* kConfig = R"(
function gamelua.offlineShopConfigure()
    local params = _G.VariantConfig and _G.VariantConfig.params
    if not params then return end
    local rewards = params.currencyRewards
    if rewards and rewards.levelCompletion then
        rewards.levelCompletion.perStar = {3, 5, 7}
        rewards.episodeCompletion = 300
    end
    local offers = params.targetedOffers and params.targetedOffers.offerPool
    for i = #(offers or {}), 1, -1 do
        if offers[i].payType ~= 'coin' then table.remove(offers, i) end
    end
end
gamelua.offlineShopConfigure()
)";

constexpr const char* kLevelCompleted = R"(
local entry = LevelCompletedPage.onEntry
function LevelCompletedPage:onEntry(...)
    gamelua.offlineShopConfigure()
    return entry(self, ...)
end
)";

// CoinShop.open(onPurchase, onClose, screen, ...): the coin counters open the
// shop, and so do purchases with too few coins, which get a message instead.
constexpr const char* kCoinShop = R"(
local COUNTERS = {
    ['Episode selection'] = true, ['Level Completed'] = true, ['Level selection'] = true,
    ['Shop scene'] = true, ['Targeted Offer'] = true,
}
local TEXTS = {
    en = {'Not enough coins', 'Stars and finished episodes pay coins.'},
    de = {'Nicht genug Münzen', 'Sterne und abgeschlossene Episoden bringen Münzen.'},
    fr = {'Pas assez de pièces', 'Les étoiles et les épisodes terminés rapportent des pièces.'},
    it = {'Monete insufficienti', 'Stelle ed episodi completati fanno guadagnare monete.'},
    es = {'No tienes suficientes monedas', 'Las estrellas y los episodios completados dan monedas.'},
    pt = {'Moedas insuficientes', 'Estrelas e episódios concluídos dão moedas.'},
}
function open(_, on_close, screen)
    if not COUNTERS[screen] then
        local g = gamelua
        local text = TEXTS[tostring(_G.res.getLocale()):sub(1, 2)] or TEXTS.en
        local content = g.Components.make_message_popup(nil, text[1], text[2], nil)
        g.menuengine.instance.popupManager:push(g.menuengine.model.popup.Popup:new({content = content, show_shade = true}))
    end
    if on_close then on_close() end
end
)";

// The shop asks the store for the Mighty Eagle's price (IAP.await_info) and
// buys it through Payment.purchaseRequest. Here the price is in coins, and a
// purchase takes the coins and unlocks the eagle as a restored purchase does;
// EID_IAP_MIGHTY_EAGLE_PURCHASED shows it as bought in the shop.
constexpr const char* kEaglePrice = R"(
local EAGLE_PRICE = 1500
)";
constexpr const char* kIap = R"(
-- await_info returns a state ({tick, reset}) that the shop runs after it has
-- reset its plates; the eagle's answer comes on its first tick.
local info = await_info
function await_info(responders, ...)
    local respond = responders and responders[mightyEagleIapId]
    if not respond or gamelua.mightyEagle.isEaglePurchased() then return info(responders, ...) end
    responders[mightyEagleIapId] = nil
    local state = info(responders, ...)
    local answered = false
    return {
        tick = function(...)
            if not answered then
                answered = true
                respond({status = 'SUCCESS', info = {price = tostring(EAGLE_PRICE)}})
            end
            return state.tick(...)
        end,
        reset = function(...)
            answered = false
            if state.reset then return state.reset(...) end
        end,
    }
end
local payment = gamelua.Payment
local purchase = payment.purchaseRequest
function payment.purchaseRequest(iapid, respond, ...)
    if iapid ~= mightyEagleIapId then return purchase(iapid, respond, ...) end
    local g = gamelua
    local currency = g.Consumables.currencyId
    if g.Bank.getAmount(currency) >= EAGLE_PRICE then
        g.Bank.withdraw(currency, EAGLE_PRICE)
        g.mightyEagle.restoreMightyEagle()
        g.eventManager:send({id = g.events.EID_IAP_MIGHTY_EAGLE_PURCHASED, product_id = iapid})
        g.Components.offlineShopRefreshCoins()
    else
        g.CoinShop.open(nil, nil, 'Mighty Eagle')
    end
    respond({status = 'FAILURE', reason = 'USER_CANCEL'})
end
)";

// The coin counters (makeCurrencyDisplay), to show the coins left after a
// purchase outside the game's own code. The eagle's buy button shows its price
// as text only; it gets a coin, placed as the shop places its coin prices.
constexpr const char* kComponents = R"(
local function coin_price(text)
    local g = gamelua
    local T = g.Transformation
    local group = {x = 0, y = 0, transformation = text.transformation, items = {text}}
    text.transformation = T.identity()
    local coin = g.scenegraph.makeSprite(g.Consumables.currencySprites.small)
    coin.transformation = T.new(text:getHeight() * 1.05 / coin:getHeight())
    coin.x = coin:getWidth() * 0.6 + text:getWidth() / 2
    local shift = -((text.x - text:getWidth() / 2) + (coin.x + coin:getWidth() / 2)) / 2
    text.x, coin.x = text.x + shift, coin.x + shift
    group.items[2] = coin
    return group
end
local make_button = makeButton
function makeButton(def, ...)
    local icon = type(def) == 'table' and def.icon
    if icon and icon.textItem and icon.text == tostring(EAGLE_PRICE) and def.background
        and def.background.name == 'STORE_BUTTON_BUY' then
        def.icon = coin_price(icon)
    end
    return make_button(def, ...)
end
local displays = setmetatable({}, {__mode = 'k'})
local make_display = makeCurrencyDisplay
function makeCurrencyDisplay(...)
    local display = make_display(...)
    if type(display) == 'table' then displays[display] = true end
    return display
end
function offlineShopRefreshCoins()
    for display in pairs(displays) do display:resetQuantity(true) end
end
)";

class OfflineShop final : public slingshot::Mod {
public:
    OfflineShop() {
        using slingshot::mods::append_lua;
        append_lua("data/scripts/options.lua", kConfig);
        append_lua("data/scripts/menu/LevelCompletedPage.lua", kLevelCompleted);
        append_lua("data/scripts/menu/CoinShop.lua", kCoinShop);
        const std::string eagle_price = kEaglePrice;
        append_lua("data/scripts/iap.lua", eagle_price + kIap);
        append_lua("data/scripts/Components.lua", eagle_price + kComponents);
    }
};

}  // namespace

SLINGSHOT_MOD(OfflineShop, "offline-shop",
              "The Mighty Eagle costs coins, levels pay more coins; no coin packs");
