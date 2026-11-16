// SPDX-License-Identifier: GPL-3.0-or-later
// A shop without real money. Star Wars has no currency: the episodes it sold
// open with progress, and the star targets that pay Mighty Falcons pay more.
//
//   Path of the Jedi (Dagobah)   when every level of the six episodes is
//                                finished (the game: 3-starred)
//   Boba Fett Missions           at BOBA_STARS stars (the game: jetpacks open
//                                the first 10 levels, the rest was sold)
//
// Each of the game's 24 star targets (30 to 860 stars) pays FALCONS falcons
// instead of 5, and BUNDLES of every power-up (shown in the level's
// power-up bar). A save that passed targets
// before is paid the difference once.
//
// The shop sold packs of the Mighty Falcon and each power-up. Here it offers
// each of the five once more, as unlimited (its count no longer goes down), for
// stars that are not spent: the first at UNLIMITED_FIRST of all stars, each
// further one at more, the last at UNLIMITED_LAST. The power-up bundles are
// gone.
#include <string>

#include "common/game_scripts.hpp"
#include "mod.hpp"

namespace {

constexpr const char* kGame = R"(
local FALCONS = 15
local BUNDLES = 3
local BOBA_STARS = 200

local original_falcons
local function raise_targets()
    if original_falcons then return end
    original_falcons = {}
    for i, target in ipairs(starTargetsForFalcons) do
        original_falcons[i] = target[2]
        if target[2] > 0 then target[2] = FALCONS end
    end
end

local check_falcons = checkFalconsFromStars
function checkFalconsFromStars(...)
    raise_targets()
    local root = g_gamePurchasesTable.root
    local before = SettingsWrapper:getNextStarGoalIndex() - 1
    local falcons, stars = check_falcons(...)
    local after = SettingsWrapper:getNextStarGoalIndex() - 1
    local paid = root.offlineShopTargets
    if paid == nil then
        -- Targets reached without the mod paid the game's falcons.
        local extra = 0
        for i = 1, before do
            if original_falcons[i] > 0 then extra = extra + FALCONS - original_falcons[i] end
        end
        if extra > 0 then
            falcons = (falcons or 0) + extra
            if not stars or stars == 0 then stars = starTargetsForFalcons[before][1] end
        end
        paid = 0
    end
    if after > paid then
        -- The game's bundles leave out the clone bird.
        PowerupManager:addBundle(BUNDLES * (after - paid))
        PowerupManager:addPowerUp('cloneBird', BUNDLES * (after - paid))
    end
    root.offlineShopTargets = after
    savePurchases()
    return falcons, stars
end

local function normal_levels_finished()
    for _, folder in ipairs(actions.myLevelFolders.normalLevelFolders) do
        for _, level in ipairs(actions.myLevels[folder] or {}) do
            local meta = actions.myLevelsMetadata[level]
            if not (meta and meta.type) and (getLevelStarsFromHighscores(level) or 0) == 0 then return false end
        end
    end
    return true
end

local check_dagobah = checkDagobahUnlock
function checkDagobahUnlock(...)
    if check_dagobah(...) then return true end
    if isDagobahAvailableForDevice() and not isDagobahUnlocked() and normal_levels_finished() then
        unlockDagobah()
        return true
    end
    return false
end

-- True when the Boba Fett Missions open now.
function offlineShopCheckBoba()
    if isBobaFettEpisodeUnlocked() or countCollectedStars() < BOBA_STARS then return false end
    unlockBobaFettEpisode()
    return true
end

-- The game's texts are English only.
local TEXTS = {
    stars = 'Stars: %d / %d', episodes = 'Finish all 6 episodes', collected = '%d stars collected!',
    unlocked = 'Unlocked',
    unlimited = 'Unlimited',
    dagobah = 'Ready for Jedi training on Dagobah and a new lightsaber? Unlock it by finishing every level of the six episodes.',
    boba = "Challenge the galaxy's greatest bounty hunter in 10 new Boba Fett Missions! Find his 5 jetpacks across the galaxy or collect %d stars!",
}
local function texts() return TEXTS end

-- The shop's text for an item, nil for the game's own.
function offlineShopText(key) return string.format(texts()[key], BOBA_STARS) end

-- Unlimited Mighty Falcon and power-ups (offlineShopIsUnlimited in
-- settingsWrapper.lua).
local UNLIMITED = {'falcon', 'cloneBird', 'detonator', 'lightsaberSling', 'blasterDroid'}
local UNLIMITED_FIRST, UNLIMITED_LAST = 0.60, 0.85  -- shares of all stars
local all_stars
local function max_stars()
    if not all_stars then
        all_stars = 0
        for folder, levels in pairs(actions.myLevels) do
            if folder ~= 'nextEpisode' then  -- the "coming soon" episode
                for _, level in ipairs(levels) do
                    local meta = actions.myLevelsMetadata[level]
                    if not (meta and meta.type) then all_stars = all_stars + 3 end
                end
            end
        end
    end
    return all_stars
end
-- The stars the next unlimited power-up needs.
function offlineShopUnlimitedStars()
    local bought = 0
    for _, name in ipairs(UNLIMITED) do
        if offlineShopIsUnlimited(name) then bought = bought + 1 end
    end
    local share = UNLIMITED_FIRST + (UNLIMITED_LAST - UNLIMITED_FIRST) * bought / (#UNLIMITED - 1)
    return math.ceil(max_stars() * share)
end
-- The unlimited power-up's button: its text, whether it has a star after the
-- text, and whether it can be clicked.
function offlineShopUnlimitedButton(name)
    if offlineShopIsUnlimited(name) then return texts().unlocked, false, false end
    local stars, needed = countCollectedStars(), offlineShopUnlimitedStars()
    return string.format('%d / %d', stars, needed), true, stars >= needed
end
function offlineShopUnlockUnlimited(name)
    if offlineShopIsUnlimited(name) or countCollectedStars() < offlineShopUnlimitedStars() then return false end
    local root = g_gamePurchasesTable.root
    root.offlineShopUnlimited = root.offlineShopUnlimited or {}
    root.offlineShopUnlimited[name] = true
    -- At least one, so that it can be used.
    if name == 'falcon' then
        local amount = SettingsWrapper:getMEAmount() or 0
        if amount < 1 then SettingsWrapper:addMightyEagles(1 - amount) end
    elseif (SettingsWrapper:getPowerUpAmount(name) or 0) < 1 then
        SettingsWrapper:setPowerUpAmount(name, 1)
    end
    savePurchases()
    return true
end

-- The title of the Boba Fett popup (the game's: "All jetpacks collected!").
function offlineShopBobaTitle() return string.format(texts().collected, BOBA_STARS) end

-- The shop's label for a locked episode, nil when it is open.
function offlineShopCondition(episode)
    local t = texts()
    if episode == 'bobaFett_1' and not isBobaFettEpisodeUnlocked() then
        return string.format(t.stars, countCollectedStars(), BOBA_STARS)
    elseif episode == 'dagobah_1' and not isDagobahUnlocked() then
        return t.episodes
    end
end
)";

// An unlimited falcon or power-up keeps its count.
constexpr const char* kSettingsWrapper = R"(
function offlineShopIsUnlimited(name)
    local root = g_gamePurchasesTable and g_gamePurchasesTable.root
    return root and root.offlineShopUnlimited and root.offlineShopUnlimited[name] or false
end
-- The level counts the falcons left as this minus the called but not yet shot
-- ones: an unlimited falcon always has one left.
local falcons = SettingsWrapper.getMEAmount
function SettingsWrapper:getMEAmount(...)
    local amount = falcons(self, ...)
    if not offlineShopIsUnlimited('falcon') then return amount end
    local in_play = (numMightyEaglesCalled or 0) - (numMightyEaglesShot or 0)
    return math.max(amount or 0, in_play + 1)
end
local reduce = SettingsWrapper.reduceEagle
function SettingsWrapper:reduceEagle(...)
    if offlineShopIsUnlimited('falcon') then return end
    return reduce(self, ...)
end
local set_amount = SettingsWrapper.setPowerUpAmount
function SettingsWrapper:setPowerUpAmount(name, amount, ...)
    if offlineShopIsUnlimited(name) then amount = math.max(amount or 0, self:getPowerUpAmount(name) or 0, 1) end
    return set_amount(self, name, amount, ...)
end
)";

// Level HUD: an unlimited power-up's count box shows a check.
constexpr const char* kCheckLua = R"(
local function check_in(box)
    if box and not box:getChild('offlineShopCheck') then
        local check = ui.Image:new({name = 'offlineShopCheck'})
        check:setImage('ICON_CHECK')
        check.scaleX, check.scaleY = 0.6, 0.6
        box:addChild(check)
    end
end
)";
constexpr const char* kGameScene = R"(
local function falcon_check(self)
    if not offlineShopIsUnlimited('falcon') then return end
    local count = self:getChild('eaglesAvailable')
    if count then count.visible = false end
    local button = self:getChild('MEButton')
    check_in(button and button:getChild('MEButtonSmallBox'))
end
for _, name in ipairs({'setupFrameElements', 'updateMELeft'}) do
    local original = GameScene[name]
    GameScene[name] = function(self, ...)
        local results = {original(self, ...)}
        falcon_check(self)
        return unpack(results)
    end
end
)";
constexpr const char* kPowerupSlider = R"(
local update = PowerupSlider.updateVisuals
function PowerupSlider:updateVisuals(...)
    local results = {update(self, ...)}
    for _, def in ipairs(self.btnTable or {}) do
        local button = gamelua.offlineShopIsUnlimited(def.powerUp) and self:getChild(def.powerUp)
        if button then
            local count = button:getChild('counterText')
            if count then count.visible = false end
            check_in(button:getChild('counterBackground'))
        end
    end
    return unpack(results)
end
)";

// Level end: the Boba Fett popup, after the game's own popups.
constexpr const char* kLevelEnd = R"(
local entry = LevelComplete.onEntry
function LevelComplete:onEntry(...)
    local results = {entry(self, ...)}
    self.offlineShopBoba = offlineShopCheckBoba()
    return unpack(results)
end
local update = LevelEnd.update
function LevelEnd:update(...)
    local results = {update(self, ...)}
    -- When the game shows its popups: the door is open, or a tap skipped it.
    local ready = self.showPopups or (self.timer or 0) > 5 and (self.doorState or 0) >= doorTable.doorCompletedState
    if self.offlineShopBoba and ready and not showingRewardPopup
        and not ((self.falconsToAdd or 0) > 0 or self.openBonusLevel or self.unlockDagobah) then
        self.offlineShopBoba = false
        local popup = ui.RewardPopup:new({bobaEpisodeUnlocked = true, inMenuSpace = false, returnValue = ''})
        local title = popup:getChild('title')
        if title then title.text = offlineShopBobaTitle() end
        eventManager:notify({id = events.EID_PUSH_FRAME, target = popup})
        showingRewardPopup = true
    end
    return unpack(results)
end
)";

// Shop items (Shop.data, from ShopData.lua): groups of packs, each group
// starting with firstElementOfGroup. Of each falcon and power-up group the
// biggest pack stays, as the unlimited power-up; the bundles go. The shop reads
// them from Shop.processData on.
constexpr const char* kShopData = R"(
local function offline_items(data)
    local groups = {}
    for _, item in ipairs(data) do
        if item.firstElementOfGroup or #groups == 0 then groups[#groups + 1] = {} end
        table.insert(groups[#groups], item)
    end
    local items = {}
    for _, group in ipairs(groups) do
        local first, last = group[1], group[#group]
        if first.purchaseType == 'EPISODE' then
            for _, item in ipairs(group) do items[#items + 1] = item end
        elseif first.powerupName ~= 'bundle' then
            -- The pack with the shortcut: the biggest has its amount drawn in.
            local item = last
            for _, pack in ipairs(group) do
                if pack.generateShortcut then item = pack end
            end
            item.firstElementOfGroup, item.lastElementOfGroup = true, true
            item.generateShortcut, item.generateAmount = true, true
            item.powerupName = first.powerupName
            item.offlineShopUnlimited = first.powerupName
            item.priceImage = nil  -- the pack's amount
            if first.powerupName == 'falcon' then item.image = 'FALCON_HUGE_PACK' end  -- without the amount
            items[#items + 1] = item
        end
    end
    return items
end
local process = Shop.processData
function Shop.processData(...)
    if not Shop.offlineShopItems then
        Shop.offlineShopItems = true
        Shop.data = offline_items(Shop.data or {})
    end
    return process(...)
end
)";

// Shop: an unlimited power-up shows what it needs, and opens with a click on
// its button; a locked episode shows what opens it.
constexpr const char* kShop = R"(
local DESCRIPTIONS = {dagobah_1 = 'dagobah', bobaFett_1 = 'boba'}
local shown = setmetatable({}, {__mode = 'k'})
-- Draws a button `factor` times as wide (its update sets the scale each frame).
local function widen(button, factor)
    if not button.offlineShopDraw then
        button.offlineShopDraw = button.draw
        button.draw = function(b, ...)
            local scale = b.scaleX
            b.scaleX = scale * (b.offlineShopWiden or 1)
            local results = {pcall(b.offlineShopDraw, b, ...)}
            b.scaleX = scale
            if not results[1] then error(results[2], 0) end
            return unpack(results, 2)
        end
    end
    button.offlineShopWiden = factor
end
local function offline_price(self)
    local data = self.data
    if not data then return end
    shown[self] = true
    local g = gamelua
    local button = self:getChild('purchaseButton')
    local price = self:getChild('priceText')
    for _, child in ipairs({button, price, self:getChild('spinner')}) do child.visible = false end
    button.returnValue = nil
    local label, buyable, description, star
    if data.offlineShopUnlimited then
        label, star, buyable = g.offlineShopUnlimitedButton(data.offlineShopUnlimited)
        data.offlineShopItem = self
        -- "Unlimited" below the icon, where the pack's amount was.
        local box = self:getChild('bgBox')
        local image = box and box:getChild('image')
        if image then
            local title = box:getChild('offlineShopTitle')
            if not title then
                title = ui.Text:new({name = 'offlineShopTitle', text = ''})
                title.getFontFunction = g.getFontHeadline
                title.hanchor = 'HCENTER'
                box:addChild(title)
            end
            title.text = g.offlineShopText('unlimited')
            local _, h = box:getSize()
            h = h or 0
            image.x, image.y = 0, -0.12 * h
            local glow = box:getChild('powerupBgGlowImage')
            if glow then glow.x, glow.y = image.x, image.y end
            title.x, title.y = 0, 0.12 * h
        end
        if g.offlineShopIsUnlimited(data.offlineShopUnlimited) then
            local gradient = self:getChild('gradientSprite')
            if gradient then gradient:setImage('BG_BOX_GRADIENT_UNLOCKED') end
        end
    elseif data.purchaseType == 'EPISODE' then
        label = g.offlineShopCondition(data.episode)
        description = DESCRIPTIONS[data.episode]
    end
    local text = description and self:getChild('text')
    if text then
        text.text = g.offlineShopText(description)
        text:clip()
    end
    if label and (button.w or 0) > 0 then
        -- Plain text in place of the price. The price font has only digits.
        price.getFontFunction = g.getFontBasic
        price.textBoxSize = button.w * 3
        price.text = label
        price:clip()
        price.visible = true
        if data.offlineShopUnlimited then
            -- The stars on the price button, dimmed until there are enough.
            button.visible = star
            button.alpha = buyable and 1 or 0.5
            if buyable then button.returnValue = 'PURCHASE_ITEM' end
        end
    end
    local icon = self:getChild('offlineShopStar')
    price.hanchor, price.x = 'HCENTER', button.x
    widen(button, 1)
    if star and label then
        if not icon then
            icon = ui.Image:new({name = 'offlineShopStar'})
            icon:setImage('LEVEL_STAR_FILLED_MIDDLE')
            self:addChild(icon)
        end
        -- Text and star (as high as the letters) centred on the button, which
        -- widens when they would come closer than PADDING to its ends.
        local PADDING = 0.35 * (button.h or 0)
        local star_scale = 0.7 * price:getHeight() / (icon.h or 1)
        local star_w = (icon.w or 0) * star_scale
        local gap = 0.25 * star_w
        local width = (price.widestLine or 0) + gap + star_w
        local button_w = button.w or 0
        widen(button, math.max(1, (width + 2 * PADDING) / math.max(button_w, 1)))
        price.hanchor = 'LEFT'
        price.x = button.x - 0.5 * width
        icon.scaleX, icon.scaleY = star_scale, star_scale
        icon.x, icon.y = price.x + width - 0.5 * star_w, price.y
        icon.visible = true
    elseif icon then
        icon.visible = false
    end
end
local add = ShopItem.addPurchaseItems
function ShopItem:addPurchaseItems(...)
    local results = {add(self, ...)}
    offline_price(self)
    return unpack(results)
end
local layout = ShopItem.layout
function ShopItem:layout(...)
    local results = {layout(self, ...)}
    offline_price(self)
    return unpack(results)
end
-- The shop centres a falcon or power-up page between its 4 packs; the single
-- unlimited one is centred on itself.
local init = Shop.init
function Shop:init(...)
    local results = {init(self, ...)}
    local anchors = self.scrollFrame and self.scrollFrame.anchors
    for index, button in pairs(self.shortcutButtons or {}) do
        local data = button.meta and button.meta[3]
        local item = data and data.offlineShopItem
        if item and anchors and anchors[index] then anchors[index].x = item.x end
    end
    if anchors then self.scrollFrame:setAnchors(anchors) end
    return unpack(results)
end
-- A shortcut's count box shows a check for an unlimited power-up.
local shop_layout = Shop.layout
function Shop:layout(...)
    local results = {shop_layout(self, ...)}
    for _, button in pairs(self.shortcutButtons or {}) do
        local data = button.meta and button.meta[3]
        local box = button:getChild('availableBG')
        local unlimited = data and data.offlineShopUnlimited and gamelua.offlineShopIsUnlimited(data.offlineShopUnlimited)
        if box and unlimited then
            local count = box:getChild('availableText')
            if count then count.visible = false end  -- refreshItems sets its text too
            if not box:getChild('offlineShopCheck') then
                local check = ui.Image:new({name = 'offlineShopCheck'})
                check:setImage('ICON_CHECK')
                check.scaleX, check.scaleY = 0.6, 0.6
                box:addChild(check)
            end
        end
    end
    return unpack(results)
end
function Shop:purchaseItem(data)
    if data and data.offlineShopUnlimited and gamelua.offlineShopUnlockUnlimited(data.offlineShopUnlimited) then
        for item in pairs(shown) do offline_price(item) end
        self:layout()  -- the shortcuts' counts
        return
    end
    return -1
end
)";

// Purchases elsewhere (the power-up and falcon popups) fail at once instead of
// after the 60 s timeout.
constexpr const char* kIap = R"(
local purchase = purchaseShopItem
function purchaseShopItem(...)
    local results = {purchase(...)}
    g_purchasePendingTimer = 0
    return unpack(results)
end
)";

class OfflineShop final : public slingshot::Mod {
public:
    OfflineShop() {
        using slingshot::mods::append_lua;
        append_lua("data/scripts/game.lua", kGame);
        append_lua("data/scripts/menus/level_end.lua", kLevelEnd);
        append_lua("data/scripts/settingsWrapper.lua", kSettingsWrapper);
        const std::string check = kCheckLua;
        append_lua("data/scripts/gamescene.lua", check + kGameScene);
        append_lua("data/scripts/menus/PowerupSlider.lua", check + kPowerupSlider);
        append_lua("data/scripts/shop/Shop.lua", std::string(kShopData) + kShop);
        append_lua("data/scripts/iap.lua", kIap);
    }
};

}  // namespace

SLINGSHOT_MOD(OfflineShop, "offline-shop",
              "Dagobah and Boba Fett open with progress, stars pay more and unlock unlimited power-ups");
