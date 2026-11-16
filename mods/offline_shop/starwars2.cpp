// SPDX-License-Identifier: GPL-3.0-or-later
// Credits without real money. Star Wars II sells only credit packs: every
// chapter, its other side and Master Your Destiny also open with stars, and
// characters cost credits (56 of them for good: 116,800 in all).
//
// Stars pay credits as in the game's PC version (on Android they pay nothing):
// a new star the chapter's PCCreditsPerStar, a replayed star
// creditsRewardPerStar. Both rise here with the chapter:
//
//   chapter                      new star       replayed star
//   1                            15, 30, 85     3, 5, 7      (PC: 10, 20, 60)
//   2                            30, 75, 150    3, 6, 9      (PC: 20, 50, 100)
//   3                            30, 80, 160    4, 6, 10
//   4                            35, 85, 170    4, 7, 11
//   5, Rebels, bonus             35, 90, 180    5, 8, 12
//   Master Your Destiny          110, 270, 540  5, 10, 15    (PC: 60, 150, 300)
//
// (PC replays: 5, 10, 15.) The 362 levels pay about 135,400 credits when all
// are 3-starred: every character, with credits to spare. Stars earned before
// are paid once.
#include <string>

#include "common/game_scripts.hpp"
#include "mod.hpp"

namespace {

constexpr const char* kRates = R"(
-- By chapter folder (chapter_<name>_light / _dark).
local RATES = {
    ['1'] = {new = {15, 30, 85}, replay = {3, 5, 7}},
    ['2'] = {new = {30, 75, 150}, replay = {3, 6, 9}},
    ['3'] = {new = {30, 80, 160}, replay = {4, 6, 10}},
    ['4'] = {new = {35, 85, 170}, replay = {4, 7, 11}},
    ['5'] = {new = {35, 90, 180}, replay = {5, 8, 12}},
    rebels = {new = {35, 90, 180}, replay = {5, 8, 12}},
    bonus = {new = {35, 90, 180}, replay = {5, 8, 12}},
    IAP = {new = {110, 270, 540}, replay = {5, 10, 15}},
}
local function rates(folder)
    local name = tostring(folder or ''):match('^chapter_(.-)_[a-z]+$')
    return name and RATES[name]
end
)";

// The level end pays as on PC (giveCreditsFromStars is true on Windows only),
// with this chapter's rates.
constexpr const char* kLevelEnd = R"(
function giveCreditsFromStars() return true end
local function set_rates()
    local r = rates(currentFolder)
    local meta = r and getEpisodeMetadata(currentFolder)
    if not meta then return end
    meta.PCCreditsPerStar = r.new
    creditsRewardPerStar = r.replay
end
local entry = LevelComplete.onEntry
function LevelComplete:onEntry(...)
    set_rates()
    return entry(self, ...)
end
)";

// Stars earned before pay their new-star credits once. MainMenu.update, as
// onEntry runs before the save is loaded.
constexpr const char* kMainMenu = R"(
local function top_up()
    local g = gamelua
    local root = g.g_gamePurchasesTable and g.g_gamePurchasesTable.root
    if not root or root.offlineShopStarCredits or not (g.actions and g.actions.myLevels) then return end
    local credits = 0
    for folder, levels in pairs(g.actions.myLevels) do
        local r = rates(folder)
        for _, level in ipairs(r and levels or {}) do
            local meta = g.actions.myLevelsMetadata[level]
            if not (meta and meta.type) then
                for star = 1, math.min(g.getLevelStarsFromHighscores(level) or 0, 3) do
                    credits = credits + r.new[star]
                end
            end
        end
    end
    root.offlineShopStarCredits = true
    g.savePurchases()
    if credits > 0 then g.addCoins(credits) end
end
local update = MainMenu.update
function MainMenu:update(...)
    top_up()
    return update(self, ...)
end
)";

constexpr const char* kCoinsPage = R"(
local entry = CoinsPage.onEntry
function CoinsPage:onEntry(...)
    local results = {entry(self, ...)}
    self:close()
    return unpack(results)
end
)";

// The game's mode for builds without purchases (isIapEnabled false), only for
// these screens: the credit counter has no "+", and too few credits show how
// many are missing with one button instead of offering credit packs.
constexpr const char* kNoIap = R"(
local function without_iap(class, name)
    local original = class[name]
    class[name] = function(...)
        local g = gamelua
        local iap = g.isIapEnabled
        g.isIapEnabled = function() return false end
        local results = {pcall(original, ...)}
        g.isIapEnabled = iap
        if not results[1] then error(results[2], 0) end
        return unpack(results, 2)
    end
end
)";
constexpr const char* kCoinButton = R"(
without_iap(CoinButton, 'init')
without_iap(CoinButton, 'layout')
)";
constexpr const char* kRedirect = R"(
local TEXTS = {
    en = 'Earn stars to get more credits!',
    de = 'Sammle Sterne, um weitere Credits zu verdienen!',
    fr = 'Obtenez des étoiles pour gagner plus de crédits\194\160!',
    it = 'Ottieni stelle per guadagnare altri crediti!',
    es = '¡Consigue estrellas para hacerte con más créditos!',
    pt = 'Conquiste estrelas para ganhar mais créditos!',
    ja = 'スターを集めてクレジットを獲得しよう！',
    ru = 'Собирай звёзды, чтобы заработать больше кредитов!',
}
without_iap(PurchaseRedirectDialog, 'init')
without_iap(PurchaseRedirectDialog, 'layout')
local init = PurchaseRedirectDialog.init
function PurchaseRedirectDialog:init(...)
    local results = {init(self, ...)}
    local text = TEXTS[tostring((gamelua.res or res).getLocale()):sub(1, 2)] or TEXTS.en
    if self.textMore then self.textMore:setText(text) end
    return unpack(results)
end
)";

class OfflineShop final : public slingshot::Mod {
public:
    OfflineShop() {
        using slingshot::mods::append_lua;
        const std::string rates = kRates;
        append_lua("data/scripts/menus/level_end.lua", rates + kLevelEnd);
        append_lua("data/scripts/menus/main_menu.lua", rates + kMainMenu);
        append_lua("data/scripts/shop/CoinsPage.lua", kCoinsPage);
        const std::string no_iap = kNoIap;
        append_lua("data/scripts/menus/CoinButton.lua", no_iap + kCoinButton);
        append_lua("data/scripts/shop/PurchaseRedirectDialog.lua", no_iap + kRedirect);
    }
};

}  // namespace

SLINGSHOT_MOD(OfflineShop, "offline-shop",
              "Stars pay credits, enough for every character; no credit packs");
