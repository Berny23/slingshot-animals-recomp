// SPDX-License-Identifier: GPL-3.0-or-later
// A shop without real money. Space has no currency: the level packs open with
// progress, and stars pay Space Eagles and power-ups.
//
//   Danger Zone            at DANGER_ZONE_STARS stars
//   Beak Impact part 2     when part 1 is finished (the game: 3-starred)
//   Mirror World           when episodes 1-9 are finished (the game: 3-starred)
//   Kellogg's              open (the promotion switched it on)
//
// Every star earned for the first time counts: each EAGLE_STARS stars pay a
// Space Eagle, each BUNDLE_STARS stars one of every power-up. Mirror World stars
// count too. A save with stars from before is paid once for all of them.
//
// The shop sold packs of Space Eagles and each power-up. Here it offers each of
// the five once, as unlimited (its count no longer goes down), for stars that
// are not spent: the first at UNLIMITED_FIRST of the stars of the episodes,
// Danger Zone and Mirror World, each further one at more, the last at
// UNLIMITED_LAST. The bundles are gone, as stars pay them.
#include <string>

#include "common/game_scripts.hpp"
#include "mod.hpp"

namespace {

constexpr const char* kSettings = R"(
local DANGER_ZONE_STARS = 120
local EAGLE_STARS = 3
local BUNDLE_STARS = 12
local STORY = {'theme1', 'theme2', 'theme3', 'theme4', 'theme5', 'theme6', 'theme7', 'theme81', 'theme82', 'theme9', 'theme10'}
local MIRROR_EPISODES = {'theme1', 'theme2', 'theme3', 'theme4', 'theme5', 'theme6', 'theme7', 'theme81', 'theme82', 'theme9'}
-- The game's languages (supportedLanguages): English and Japanese.
local TEXTS = {
    en = {stars = 'Stars: %d / %d', part1 = 'Finish part 1', episodes = 'Finish episodes 1-9', unlimited = 'Unlimited'},
    ja = {stars = 'スター: %d / %d', part1 = 'パート1をクリア', episodes = 'エピソード1-9をクリア', unlimited = '無制限'},
}
-- Shop group, power-up (offlineShopIsUnlimited) and shop image without amount.
local UNLIMITED = {
    {group = 'spaceeagle', name = 'spaceeagle', sprite = 'LOZENGE_SPACE_EAGLE'},
    {group = 'flockofbirds', name = 'flock', sprite = 'LOZENGE_FLOCK'},
    {group = 'antennaegg', name = 'antenna', sprite = 'LOZENGE_EGG'},
    {group = 'inflatepigs', name = 'inflate', sprite = 'LOZENGE_PIG'},
    {group = 'wingman', name = 'wingman', sprite = 'LOZENGE_WINGMAN'},
}
local UNLIMITED_FIRST, UNLIMITED_LAST = 0.60, 0.85  -- shares of the stars
local function text(key)
    local t = TEXTS[tostring(g_currentLocale):sub(1, 2)] or TEXTS.en
    return t[key] or TEXTS.en[key]
end
local function levels(folder)
    local list = {}
    for _, level in ipairs(actions.myLevels[folder] or {}) do
        local meta = getLevelMetadata(level)
        if not (meta and meta.type) then list[#list + 1] = level end  -- not comics or menus
    end
    return list
end
local function stars(folders, mirror)
    local sum = 0
    for _, folder in ipairs(folders) do
        for _, level in ipairs(levels(folder)) do
            sum = sum + (getLevelStarsFromHighscores(level) or 0)
            if mirror then sum = sum + (getLevelStarsFromHighscores(level, true) or 0) end
        end
    end
    return sum
end
local function finished(folders)
    for _, folder in ipairs(folders) do
        for _, level in ipairs(levels(folder)) do
            if (getLevelStarsFromHighscores(level) or 0) == 0 then return false end
        end
    end
    return true
end
local function story_stars() return stars(STORY) end
)";

// The checks, after a level and in the episode selection.
constexpr const char* kGame = R"(
local function purchased(episode) return SettingsWrapper:isEpisodePurchased(episode) end
-- The game's reward popup (as addMightyEagles shows it).
local function announce(text_id, icons, title)
    requireFile('menu/AchievementPopup.lua')
    local popup = menu.MERewardPopup:new(nil, title or '', res.getString('TEXTS_BASIC', text_id), icons)
    GameSystem.menuManager:notificationsFrame():addChild(popup)
end
-- As the game opens Mirror World by progress (mirrorWorldHandler).
local function open(episode, text_id, icon)
    GameSystem.eventManager:notify({id = events.EID_EPISODE_UNLOCKED_BY_PROGRESS, episode = episode})
    SettingsWrapper:purchaseEpisode(episode, 'progress')
    savePurchases()
    announce(text_id, {icon})
end
local function all_stars()
    local folders = {}
    for _, folder in ipairs(actions.myLevelFolders) do
        if folder ~= 'tutorial' and folder ~= 'appData' then folders[#folders + 1] = folder end
    end
    return stars(folders, true)
end
function offlineShopCheck()
    if not (actions and actions.myLevelFolders and g_gamePurchasesTable and g_gamePurchasesTable.root) then return end
    if not purchased('dangerZone') and story_stars() >= DANGER_ZONE_STARS then
        open('dangerZone', 'TEXT_DANGER_ZONE_UNLOCKED', 'BTN_DANGER_ZONE')
    end
    if not purchased('theme82') and finished({'theme81'}) then
        -- The game's own reward for 3-starring part 1.
        actions.unlockLevels.action({episode = 'theme82', levels = 1})
        announce('TEXT_BEAK_IMPACT_UNLOCKED', {'BTN_BEAK_IMPACT'})
    end
    if not purchased('mirrorWorld') and purchased('theme82') and finished(MIRROR_EPISODES) then
        open('mirrorWorld', 'TEXT_MIRROR_WORLD_UNLOCKED', 'BTN_SHOP_MIRROR_WORLD')
    end
    -- The achievements for buying Danger Zone and Space Eagles.
    if AchievementsLogic and GameAchievements then
        if purchased('dangerZone') then
            AchievementsLogic.unlockAchievement(GameAchievements.WHERE_NO_BIRDS_HAVE_GONE_BEFORE)
        end
        if offlineShopIsUnlimited and offlineShopIsUnlimited('spaceeagle') then
            AchievementsLogic.unlockAchievement(GameAchievements.FOWL_DELIVERY)
        end
    end
    local root = g_gamePurchasesTable.root
    local paid = root.offlineShopStars or 0
    local now = all_stars()
    if now <= paid then return end
    local eagles = math.floor(now / EAGLE_STARS) - math.floor(paid / EAGLE_STARS)
    local bundles = math.floor(now / BUNDLE_STARS) - math.floor(paid / BUNDLE_STARS)
    root.offlineShopStars = now
    SettingsWrapper:addMightyEagles(eagles)
    if bundles > 0 then
        powerups.addBundle(bundles)
        announce('TEXT_EAGLE_AND_POWERUP_AWARD', {'BTN_BUNDLE', 'BTN_SPACE_EAGLE'}, 'x' .. bundles .. '  x' .. eagles)
    elseif eagles > 0 then
        announce('TEXT_EAGLE_AWARD', {'BTN_SPACE_EAGLE'}, 'x' .. eagles)
    end
    savePurchases()
end

-- Unlimited power-ups: the stars of the episodes, Danger Zone and Mirror World.
local THREE_STARS = {'theme10', 'dangerZone'}  -- besides MIRROR_EPISODES
local max_stars
local function unlimited_stars()
    if not max_stars then
        local levels_n = 0
        for _, folder in ipairs(MIRROR_EPISODES) do levels_n = levels_n + 2 * #levels(folder) end
        for _, folder in ipairs(THREE_STARS) do levels_n = levels_n + #levels(folder) end
        max_stars = 3 * levels_n
    end
    return stars(MIRROR_EPISODES, true) + stars(THREE_STARS)
end
-- The stars the next unlimited power-up needs.
function offlineShopUnlimitedStars()
    local collected = unlimited_stars()
    local bought = 0
    for _, item in ipairs(UNLIMITED) do
        if offlineShopIsUnlimited(item.name) then bought = bought + 1 end
    end
    local share = UNLIMITED_FIRST + (UNLIMITED_LAST - UNLIMITED_FIRST) * bought / (#UNLIMITED - 1)
    return collected, math.ceil(max_stars * share)
end
function offlineShopUnlimitedText() return text('unlimited') end
function offlineShopUnlockUnlimited(name)
    local collected, needed = offlineShopUnlimitedStars()
    if offlineShopIsUnlimited(name) or collected < needed then return false end
    local root = g_gamePurchasesTable.root
    root.offlineShopUnlimited = root.offlineShopUnlimited or {}
    root.offlineShopUnlimited[name] = true
    -- At least one, so that it can be used.
    if name == 'spaceeagle' then
        local amount = g_eaglePurchasesTable.root.availableEagles or 0
        if amount < 1 then SettingsWrapper:addMightyEagles(1 - amount) end
    elseif (SettingsWrapper:getPowerUpAmount(name) or 0) < 1 then
        SettingsWrapper:setPowerUpAmount(name, 1)
    end
    savePurchases()
    GameSystem.eventManager:notify({id = events.EID_CONSUMABLE_AMOUNT_CHANGED, product = name})
    if name == 'spaceeagle' then AchievementsLogic.unlockAchievement(GameAchievements.FOWL_DELIVERY) end
    return true
end
)";

constexpr const char* kLevelComplete = R"(
local entry = menu.LevelComplete.onEntry
function menu.LevelComplete:onEntry(...)
    local results = {entry(self, ...)}
    offlineShopCheck()
    return unpack(results)
end
)";

constexpr const char* kEpisodeSelection = R"(
local entry = menu.EpisodeSelection.onEntry
function menu.EpisodeSelection:onEntry(...)
    local results = {entry(self, ...)}
    offlineShopCheck()
    return unpack(results)
end
)";

constexpr const char* kSettingsWrapper = R"(
function SettingsWrapper:isKelloggsAvailable() return true end

-- An unlimited Space Eagle or power-up keeps its count.
function offlineShopIsUnlimited(name)
    local root = g_gamePurchasesTable and g_gamePurchasesTable.root
    return root and root.offlineShopUnlimited and root.offlineShopUnlimited[name] or false
end
-- The level counts the eagles left as this minus the called but not yet shot
-- ones: an unlimited eagle always has one left.
local eagles = SettingsWrapper.getMEAmount
function SettingsWrapper:getMEAmount(...)
    local amount = eagles(self, ...)
    if not offlineShopIsUnlimited('spaceeagle') then return amount end
    local in_play = (numMightyEaglesCalled or 0) - (numMightyEaglesShot or 0)
    return math.max(amount or 0, in_play + 1)
end
local reduce = SettingsWrapper.reduceEagle
function SettingsWrapper:reduceEagle(...)
    if offlineShopIsUnlimited('spaceeagle') then return end
    return reduce(self, ...)
end
local set_amount = SettingsWrapper.setPowerUpAmount
function SettingsWrapper:setPowerUpAmount(name, amount, ...)
    if offlineShopIsUnlimited(name) then amount = math.max(amount or 0, self:getPowerUpAmount(name) or 0, 1) end
    return set_amount(self, name, amount, ...)
end
)";

// The shop's groups: each Space Eagle and power-up group sells one unlimited
// product in place of its packs; the bundles go.
constexpr const char* kIap = R"(
local order = iap.SHOP_PRODUCT_GROUP_ORDER
local by_group = {}
for _, item in ipairs(UNLIMITED) do by_group[item.group] = item end
local groups = {}
for _, entry in ipairs(order) do
    local item = by_group[entry.group]
    if item then
        local id = item.group .. '.offlineunlimited'
        iap.PRODUCT_DATA[id] = {
            shopSprite = item.sprite, iapType = iap.IAP_TYPE.CONSUMABLE,
            group = iap.GROUP_DATA[item.group], offlineShopUnlimited = item.name,
        }
        groups[#groups + 1] = {id, group = entry.group}
    elseif entry.group ~= 'bundle' then
        groups[#groups + 1] = entry
    end
end
for i = #order, 1, -1 do order[i] = nil end
for i, entry in ipairs(groups) do order[i] = entry end
)";

// Buying an unlimited product unlocks it; the cancel ends the shop's wait.
constexpr const char* kPayment = R"(
local buy = iap.Payment.buyProduct
iap.Payment.buyProduct = function(id, ...)
    local data = iap.PRODUCT_DATA[id]
    if not (data and data.offlineShopUnlimited) then return buy(id, ...) end
    offlineShopUnlockUnlimited(data.offlineShopUnlimited)
    if ui.ShopProduct.offlineShopRefresh then ui.ShopProduct.offlineShopRefresh() end
    GameSystem.eventManager:notify({id = events.EID_IAP_PURCHASE_CANCELED, productId = id})
end
)";

// The shop: a locked level pack shows what opens it in place of the price.
constexpr const char* kShopProduct = R"(
local CONDITIONS = {
    ['levelpack.dangerzone'] = {episode = 'dangerZone', text = function()
        return string.format(text('stars'), story_stars(), DANGER_ZONE_STARS)
    end},
    ['levelpack.theme8part2'] = {episode = 'theme82', text = function() return text('part1') end},
    ['levelpack.mirrorWorld'] = {episode = 'mirrorWorld', text = function() return text('episodes') end},
}
local function show_condition(self)
    local condition = CONDITIONS[self._productId or '']
    if not condition or SettingsWrapper:isEpisodePurchased(condition.episode) then return end
    local image = self:getChild('image')
    local price = image and image:getChild('price')
    if not price then return end
    for _, name in ipairs({'spinner', 'offImage'}) do
        local child = price:getChild(name)
        if child then child:removeSelf() end
    end
    price.returnValue = nil
    self._ready = true  -- the price refresh leaves it alone
    local label = condition.text()
    local current = price:getChild('text')
    if current and current.offlineShopLabel == label then return end
    if current then current:removeSelf() end
    local t = ui.Text:new('text', label)
    t.offlineShopLabel = label
    price:addChild(t)
end
-- An unlimited product: "Unlimited" on its image, and its price button shows
-- the stars it needs (dimmed until there are enough), or "Unlocked" once bought.
local STAR = 'BEST_STAR_YELLOW'
local shown = setmetatable({}, {__mode = 'k'})
local function show_unlimited(self)
    local data = iap.PRODUCT_DATA[self._productId or '']
    local name = data and data.offlineShopUnlimited
    local image = name and self:getChild('image')
    if not image then return false end
    shown[self] = true
    self._ready = true  -- the price refresh leaves it alone
    local bought = offlineShopIsUnlimited(name)
    local collected, needed = offlineShopUnlimitedStars()
    local key = bought and 'bought' or collected .. '/' .. needed
    if self.offlineShopKey == key then return false end
    self.offlineShopKey = key
    if not image:getChild('offlineShopTitle') then
        image:addChild(ui.Text:new('offlineShopTitle', offlineShopUnlimitedText()))
    end
    local price = image:getChild('price')
    if bought then
        if price then image:removeChildByName('price') end
        if not image:getChild('unlockedLabel') then
            image:addChild(ui.Text:new('unlockedLabel', 'TEXT_IAP_UNLOCKED'))
        end
        return true
    end
    if not price then return true end
    for _, child in ipairs({'spinner', 'offImage', 'text', 'offlineShopStar'}) do
        if price:getChild(child) then price:removeChildByName(child) end
    end
    local buyable = collected >= needed
    price.returnValue = buyable and 'BUY_PRODUCT' or nil
    price:addChild(ui.Text:new('text', string.format('%d / %d', collected, needed)))
    local star = ui.Image:new('offlineShopStar')
    star:setImage(STAR)
    price:addChild(star)
    local alpha = not buyable and 0.4 or nil
    price.alpha, star.alpha = alpha, alpha
    return true
end
local function place_unlimited(self)
    local image = self.offlineShopKey and self:getChild('image')
    if not image then return end
    local title = image:getChild('offlineShopTitle')
    if title then title.x, title.y = 0, 0.18 * (image.h or 0) end
    local price = image:getChild('price')
    local text = price and price:getChild('text')
    local star = price and price:getChild('offlineShopStar')
    if not (text and star and star.h and star.h > 0) then return end
    -- Text and star (as high as the letters), centred and scaled down to fit
    -- the tag's middle.
    local text_w, text_h = text.w or 0, text.h or 0
    local width = text_w + 1.25 * text_h * star.w / star.h
    local fit = math.min(1, 0.85 * (price.w or 0) / math.max(width, 1))
    text:setScale(fit)
    local scale = fit * text_h / star.h
    star.scaleX, star.scaleY = scale, scale
    local star_w = star.w * scale
    width = fit * text_w + 1.25 * star_w
    text.x = -0.5 * width + 0.5 * fit * text_w
    star.x, star.y = 0.5 * width - 0.5 * star_w, text.y
end
function ui.ShopProduct.offlineShopRefresh()
    for product in pairs(shown) do
        if show_unlimited(product) then product:layout() end
    end
end
local layout = ui.ShopProduct.layout
function ui.ShopProduct:layout(...)
    show_condition(self)
    show_unlimited(self)
    local results = {layout(self, ...)}
    place_unlimited(self)
    return unpack(results)
end
local update = ui.ShopProduct.update
function ui.ShopProduct:update(...)
    local label = self:getChild('image')
    label = label and label:getChild('price')
    local before = label and #(label.children or {})
    show_condition(self)
    local changed = show_unlimited(self)
    local results = {update(self, ...)}
    if changed or label and #(label.children or {}) ~= before then self:layout() end
    return unpack(results)
end
)";

// Counters (shop banner, level HUD): an unlimited Space Eagle or power-up shows
// a check in place of its count. The HUD buttons have the power-up as meta.
constexpr const char* kCounterButton = R"(
local POWERUPS = {}
for _, item in ipairs(UNLIMITED) do POWERUPS[item.name] = true end
local set_counter = ui.CounterButton.setCounter
function ui.CounterButton:setCounter(amount, ...)
    local name = self.offlineShopUnlimited or self.name == 'MEButton' and 'spaceeagle'
        or POWERUPS[self.meta or ''] and self.meta
    local unlimited = name and offlineShopIsUnlimited and offlineShopIsUnlimited(name)
    local results = {set_counter(self, unlimited and 1 or amount, ...)}
    local box = self:getChild('counterBackground')
    local check = box and box:getChild('offlineShopCheck')
    if unlimited and box then
        box:getChild('counterText'):setText('')
        if not check then
            check = ui.Image:new('offlineShopCheck')
            check:setImage('ICON_CHECK')
            box:addChild(check)
        end
        local scale = 0.75 * (box.h or 0) / math.max(check.h or 1, 1)
        check.scaleX, check.scaleY, check.visible = scale, scale, true
    elseif check then
        check.visible = false
    end
    return unpack(results)
end
)";
// The banner's buttons (group_<index>) show the groups' counts.
constexpr const char* kGroupsBanner = R"(
local NAMES = {}
for _, item in ipairs(UNLIMITED) do NAMES[item.group] = item.name end
local init = ui.ShopGroupsBanner.init
function ui.ShopGroupsBanner:init(...)
    local results = {init(self, ...)}
    for i, group in ipairs(self._groupList or {}) do
        local button = NAMES[group] and self:getChild('group_' .. i)
        if button then
            button.offlineShopUnlimited = NAMES[group]
            if offlineShopIsUnlimited(NAMES[group]) then button:setCounter(1) end
        end
    end
    return unpack(results)
end
)";

class OfflineShop final : public slingshot::Mod {
public:
    OfflineShop() {
        using slingshot::mods::append_lua;
        const std::string settings = kSettings;
        append_lua("data/scripts/game.lua", settings + kGame);
        append_lua("data/scripts/menu/LevelEnd/LevelComplete.lua", kLevelComplete);
        append_lua("data/scripts/menu/EpisodeSelection.lua", kEpisodeSelection);
        append_lua("data/scripts/settingsWrapper.lua", kSettingsWrapper);
        append_lua("data/scripts/iap/iap.lua", settings + kIap);
        append_lua("data/scripts/iap/Payment.lua", kPayment);
        append_lua("data/scripts/ui/ShopProduct.lua", settings + kShopProduct);
        append_lua("data/scripts/ui/CounterButton.lua", settings + kCounterButton);
        append_lua("data/scripts/ui/ShopGroupsBanner.lua", settings + kGroupsBanner);
    }
};

}  // namespace

SLINGSHOT_MOD(OfflineShop, "offline-shop",
              "Level packs open with progress, stars pay Space Eagles and power-ups and unlock unlimited ones");
