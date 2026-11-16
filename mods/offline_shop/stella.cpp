// SPDX-License-Identifier: GPL-3.0-or-later
// Coins without real money. Stella sells only coin packs, and coins only skip
// waits or help in levels: a chapter gate opens on its timer (up to 6 h) or for
// 10-600 coins (less with more stars), locked birds open with stars or for 40.
// Gates and the star track pay coins, and so did videos:
//
//   a gate          its reward plus the video's 20 coins (coinRewardPerVideoAd)
//
// The gift boxes on the chapter maps (Sweet Tunes Stella, Artistic Willow,
// Scientist Dahlia, Princess Gale) open as the game's own list has them: the
// game read that list only from the asset service. The other Telepod skins
// come from the star track.
//
// The "not enough coins" popup and the coin counter no longer lead to the coin
// shop, whose packs have no prices offline.
#include "common/game_scripts.hpp"
#include "mod.hpp"

namespace {

// economyParametersLoaded runs with every loaded economy, the first time before
// this code: it gets the same change here.
constexpr const char* kEconomy = R"(
local function fold_video_coins(params)
    for _, gate in ipairs(params and params.gates or {}) do
        if not gate.offlineShop then
            gate.offlineShop = true
            gate.coinReward = (gate.coinReward or 0) + (gate.coinRewardPerVideoAd or 0)
        end
    end
end
local loaded = economyParametersLoaded
function economyParametersLoaded(params, ...)
    local results = {loaded(params, ...)}
    fold_video_coins(params)
    return unpack(results)
end
fold_video_coins(g_economyParameters)
)";

// The game's unlocked_gift_bird_skins.dat.
constexpr const char* kMapScreen = R"(
local GIFT_BOXES = {
    'BUTTON_LEVELSELECT_GIFT_BOX_1', 'BUTTON_LEVELSELECT_GIFT_BOX_2',
    'BUTTON_LEVELSELECT_GIFT_BOX_3', 'BUTTON_LEVELSELECT_GIFT_BOX_4',
}
offlineShopGiftBoxes = GIFT_BOXES  -- for game-statistics
local entry = ui.MapScreen.onEntry
function ui.MapScreen:onEntry(...)
    local results = {entry(self, ...)}
    if not (Assets and Assets.getAssetFilename('unlocked_gift_bird_skins.dat')) then
        giftBirdSkinConfigurationLoaded(GIFT_BOXES)
    end
    return unpack(results)
end
)";

constexpr const char* kNotEnoughCoins = R"(
local updated = ui.NotEnoughCoinsPopup.coinsUpdated
function ui.NotEnoughCoinsPopup:coinsUpdated(...)
    local results = {updated(self, ...)}
    for _, name in ipairs({'btnGetBundle', 'coinSmall', 'midbg', 'textGetCoins', 'textCoinShop', 'btnCoinShop'}) do
        local child = self:getChild(name)
        if child then child:setVisible(false) end
    end
    return unpack(results)
end
)";

// The coin counter: a click opened the coin shop, and its sprite ends in a "+"
// segment (the right PLUS_PART of it), which is clipped off.
constexpr const char* kCoinBar = R"(
local PLUS_PART = 0.23
function ui.CoinBar:onPointerEvent() end
local draw = ui.CoinBar.draw
function ui.CoinBar:draw(...)
    local x, y = self:getScreenPosition()
    local scale = self:getCumulativeScale()
    if x and scale and self.w then
        self:setClipRect({x - (self.px or 0) * scale, y - (self.py or 0) * scale,
                          (1 - PLUS_PART) * self.w * scale, self.h * scale})
    end
    return draw(self, ...)
end
)";

class OfflineShop final : public slingshot::Mod {
public:
    OfflineShop() {
        using slingshot::mods::append_lua;
        append_lua("data/scripts/economy_parameters.lua", kEconomy);
        append_lua("data/scripts/menus/map_screen/MapScreen.lua", kMapScreen);
        append_lua("data/scripts/menus/NotEnoughCoinsPopup.lua", kNotEnoughCoins);
        append_lua("data/scripts_common/ui_components/CoinBar.lua", kCoinBar);
    }
};

}  // namespace

SLINGSHOT_MOD(OfflineShop, "offline-shop",
              "Gates pay the lost video coins, the map gift boxes open; no coin shop");
