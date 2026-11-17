// SPDX-License-Identifier: GPL-3.0-or-later
// A main menu button opens a page with the progress to 100 %, the game's counters
// and its achievements; unlocks show as a banner.
//
// The game unlocks and stores achievements offline (AchievementsLogic,
// SettingsWrapper:registerAchievement); the gamer service only showed them. The
// game has their names (GameAchievements) but no descriptions; these follow the
// PC version's. "Fowl Delivery" (buying Space Eagles) and "Where No Birds Have
// Gone Before" (buying Danger Zone) are listed with offline-shop, which awards
// them for the unlimited Space Eagle and for opening Danger Zone.
//
// The progress is the mean of stars (with Mirror World), Space Eagle feathers
// (each episode's feather level included), eggsteroids and achievements. Danger
// Zone counts once it can be opened (offline-shop, or bought before), Kellogg's
// while it is available.
#include <string>

#include "common/game_scripts.hpp"
#include "common/statistics.hpp"
#include "mod.hpp"

namespace {

// Unlock banner (defined with the page).
constexpr const char* kBanner = R"(
local unlock = AchievementsLogic.unlockAchievement
function AchievementsLogic.unlockAchievement(achievement, ...)
    local new = achievement and not SettingsWrapper:hasAchievement(achievement.name)
    local results = {unlock(achievement, ...)}
    if new and SettingsWrapper:hasAchievement(achievement.name) and gameStatisticsBanner then
        pcall(gameStatisticsBanner, achievement)
    end
    return unpack(results)
end
)";

constexpr const char* kPage = R"(
local TEXTS = {
    en = {
        title = 'Statistics', progress = 'Game progress', stars = 'Stars', feathers = 'Space Eagle feathers',
        eggsteroids = 'Eggsteroids', achievements = 'Achievements', levels = 'Levels completed',
        score = 'Total score', playtime = 'Playtime', birds = 'Birds launched', pigs = 'Pigs popped',
        wood = 'Wooden blocks smashed', ice = 'Ice blocks smashed', stone = 'Stone blocks smashed',
        frozen = 'Pigs frozen', missions = 'Missions completed', unlocked = 'Unlocked',
        banner = 'Achievement unlocked',
    },
    ja = {
        title = '記録', progress = 'ゲーム達成度', stars = 'スター', feathers = 'スペースイーグルの羽根',
        eggsteroids = 'エッグステロイド', achievements = 'アチーブメント', levels = 'クリアしたレベル',
        score = 'トータルスコア', playtime = 'プレイ時間', birds = '飛ばしたバード', pigs = 'やっつけたブタ',
        wood = '壊した木のブロック', ice = '壊した氷のブロック', stone = '壊した石のブロック',
        frozen = '凍らせたブタ', missions = 'クリアしたミッション', unlocked = 'アンロック済み',
        banner = 'アチーブメント達成！',
    },
}
local function language() return tostring(res.getLocale()) end
local text = statistics.texts(TEXTS, language)
-- Thousands as the game writes them: "1 000"; the Japanese font's space is too narrow.
local function number(n)
    return statistics.number(n, language():sub(1, 2) == 'ja' and 'en' or 'fr')
end

-- Descriptions (%s: the achievement's limit), in the PC version's words.
local DESCRIPTIONS = {
    en = {
        PROFESSIONAL_LEVEL = 'Get three stars', STARMAN = 'Get %s stars',
        COSMIC_DISCOVERY = 'Find 1 eggsteroid', EGGSTEROID_HUNTER = 'Find %s eggsteroids',
        COSMIC_FEATHER = 'Get a Space Eagle feather', FEATHER_PICKER = 'Get %s Space Eagle feathers',
        VEGETABLES_OF_VENGEANCE = 'Defeat the boss of Pig Bang', HANDY_MAN = 'Defeat the boss of Cold Cuts',
        THE_BIG_BURP = 'Defeat the boss of Utopia', CURIOSITY_KILLED_THE_PIG = 'Defeat the boss of Red Planet',
        SUBTASTIC = 'Defeat the boss of Pig Dipper', SHATTERED_DREAMS = 'Defeat the boss of Cosmic Crystals',
        BIRDS_OF_COURAGE = 'Complete the last level of Danger Zone',
        WHERE_NO_BIRDS_HAVE_GONE_BEFORE = 'Open Danger Zone', FOWL_DELIVERY = 'Unlock the unlimited Space Eagle',
        ABSOLUTE_ZERO = 'Freeze the king pig', BANGING_HEADS = 'Pop pig with another pig',
        BIG_SPOOK = 'Make %s pigs scared with one big red bird', TRIPLE_FREEZE = 'Freeze %s pigs with one ice bird',
        CENTRIPETAL_FORCE = 'Shoot a bird into a lagrangian point', GRAVITATIONAL_ESCAPE = '%s birds exit atmosphere',
        WATER_JUMPER = 'Jump from water %s times', BIRD_LAUNCHER = 'Launch %s birds', PIG_POPPER = 'Pop %s pigs',
        PIGSICLE = 'Freeze %s pigs', BUBBLE_POPPER = 'Pop %s bubbles', POPCORN_ADDICT = 'Pop %s popcorns',
        GREAT_BALLS_OF_FIRE = 'Destroy %s burning asteroids', THROW_CRYSTALS_TO_THE_PIG = 'Explode %s crystal planets',
        WOODPECKER = 'Smash %s wooden blocks', ICEPICKER = 'Smash %s ice blocks', STONECUTTER = 'Smash %s stone blocks',
        BLOCK_SMASHER = 'Smash %s blocks', SMASH_MANIAC = 'Smash %s blocks',
        MENU_POPPER = 'Pop %s things in the main menu', NAME_POPPER = 'Pop %s names in the credits',
        SPACE_BIRD_FAN = 'Play for %s hours', TRUE_SPACE_BIRD_FAN = 'Play for %s hours', SPACE_BIRD_ADDICT = 'Play for %s hours',
        score = 'Get %s points', stars = 'Get %s stars',
    },
    ja = {
        PROFESSIONAL_LEVEL = 'スターを3つ獲得しよう', STARMAN = 'スターを%s個集めよう',
        COSMIC_DISCOVERY = 'エッグステロイドを1個見つけよう', EGGSTEROID_HUNTER = 'エッグステロイドを%s個見つけよう',
        COSMIC_FEATHER = 'スペースイーグルの羽根を獲得しよう', FEATHER_PICKER = 'スペースイーグルの羽根を%s本集めよう',
        VEGETABLES_OF_VENGEANCE = 'Pig Bangのボスを倒そう', HANDY_MAN = 'Cold Cutsのボスを倒そう',
        THE_BIG_BURP = 'Utopiaのボスを倒そう', CURIOSITY_KILLED_THE_PIG = 'Red Planetのボスを倒そう',
        SUBTASTIC = 'Pig Dipperのボスを倒そう', SHATTERED_DREAMS = 'Cosmic Crystalsのボスを倒そう',
        BIRDS_OF_COURAGE = 'Danger Zoneの最後のレベルをクリアしよう',
        WHERE_NO_BIRDS_HAVE_GONE_BEFORE = 'Danger Zoneを開放しよう', FOWL_DELIVERY = '無制限のスペースイーグルをアンロックしよう',
        ABSOLUTE_ZERO = 'キングピッグを凍らせよう', BANGING_HEADS = 'ブタをほかのブタにぶつけてやっつけよう',
        BIG_SPOOK = '大きな赤いバード1羽でブタを%s体こわがらせよう', TRIPLE_FREEZE = 'アイスバード1羽でブタを%s体凍らせよう',
        CENTRIPETAL_FORCE = 'バードをラグランジュ点に飛ばそう', GRAVITATIONAL_ESCAPE = 'バードを%s羽大気圏から出そう',
        WATER_JUMPER = '水から%s回ジャンプしよう', BIRD_LAUNCHER = 'バードを%s羽飛ばそう', PIG_POPPER = 'ブタを%s体やっつけよう',
        PIGSICLE = 'ブタを%s体凍らせよう', BUBBLE_POPPER = 'バブルを%s個わろう', POPCORN_ADDICT = 'ポップコーンを%s個はじけさせよう',
        GREAT_BALLS_OF_FIRE = '燃える小惑星を%s個壊そう', THROW_CRYSTALS_TO_THE_PIG = 'クリスタル惑星を%s個爆発させよう',
        WOODPECKER = '木のブロックを%s個壊そう', ICEPICKER = '氷のブロックを%s個壊そう', STONECUTTER = '石のブロックを%s個壊そう',
        BLOCK_SMASHER = 'ブロックを%s個壊そう', SMASH_MANIAC = 'ブロックを%s個壊そう',
        MENU_POPPER = 'メインメニューで%s個わろう', NAME_POPPER = 'クレジットでなまえを%s個わろう',
        SPACE_BIRD_FAN = '%s時間プレイしよう', TRUE_SPACE_BIRD_FAN = '%s時間プレイしよう', SPACE_BIRD_ADDICT = '%s時間プレイしよう',
        score = '%sポイント獲得しよう', stars = 'スターを%s個集めよう',
    },
}
local function description(key)
    local t = DESCRIPTIONS[language():sub(1, 2)] or DESCRIPTIONS.en
    return t[key] or DESCRIPTIONS.en[key]
end

local MIRROR = {theme1 = true, theme2 = true, theme3 = true, theme4 = true, theme5 = true, theme6 = true,
                theme7 = true, theme81 = true, theme82 = true, theme9 = true}
-- Feather and antenna egg levels count through their episodes.
local SKIP = {tutorial = true, appData = true, eggLevels = true, featherLevels = true}
-- Danger Zone was bought (offline-shop opens it), Kellogg's came with a promotion.
local function reachable(folder)
    if folder == 'dangerZone' then
        return offlineShopUnlimitedStars ~= nil or SettingsWrapper:isEpisodePurchased('dangerZone')
    elseif folder == 'theme_Kelloggs' then
        return SettingsWrapper:isKelloggsAvailable()
    end
    return true
end
local function folders()
    local list = {}
    for i, folder in ipairs(actions.myLevelFolders) do
        if not SKIP[folder] and reachable(folder) then list[#list + 1] = {index = i, name = folder, meta = getEpisodeMetadata(folder) or {}} end
    end
    return list
end
-- calculateEpisodeScoreAndStars: score, stars, max stars, feathers, max feathers, power-up score, best score.
local function stars()
    local done, total = 0, 0
    for _, f in ipairs(folders()) do
        local _, s, max = calculateEpisodeScoreAndStars(f.index)
        done, total = done + s, total + max
        if MIRROR[f.name] then
            _, s, max = calculateEpisodeScoreAndStars(f.index, true)
            done, total = done + s, total + max
        end
    end
    return done, total
end
-- A feather is a Space Eagle score of 100 %; each episode counts its feather level.
local function feathers()
    local done, total = 0, 0
    for _, f in ipairs(folders()) do
        if not f.meta.hideMEButton then
            local _, _, _, d, max = calculateEpisodeScoreAndStars(f.index)
            done, total = done + d, total + max
        end
    end
    return done, total
end
-- The game's own star count (Starman).
local function all_stars()
    local done = 0
    for i = 1, #actions.myLevelFolders do
        local _, s = calculateEpisodeScoreAndStars(i)
        done = done + s
    end
    return done
end
-- Not menus, comics or levels for other devices (Bonus279: Samsung only).
local function playable(level)
    local meta = getLevelMetadata(level)
    if not meta then return true end
    if meta.type or meta.ignoreScore then return false end
    if meta.deviceRestrictions then
        for _, device in ipairs(meta.deviceRestrictions) do
            if device == deviceManufacturer then return true end
        end
        return false
    end
    return true
end
local function eggsteroids()
    local done, total = 0, 0
    for _, level in ipairs(actions.myLevels.bonus or {}) do
        if playable(level) then
            total = total + 1
            if SettingsWrapper:isEggsteroidLevelUnlocked(level) then done = done + 1 end
        end
    end
    return done, total
end
local function levels_and_score()
    local done, total, score = 0, 0, 0
    for _, f in ipairs(folders()) do
        for _, level in ipairs(actions.myLevels[f.name] or {}) do
            if playable(level) then
                total = total + 1
                local h = highscores[level]
                if h and math.max(h.score or 0, h.powerUpScore or 0) > 0 then done = done + 1 end
            end
        end
        local _, _, _, _, _, _, best = calculateEpisodeScoreAndStars(f.index)
        score = score + best
    end
    return done, total, score
end
local function episode(key)
    for i, folder in ipairs(actions.myLevelFolders) do
        local meta = getEpisodeMetadata(folder)
        if meta and (meta.scoreAchievement == key or meta.starAchievement == key) then return i end
    end
end

local blocks = function(kind) return function() return SettingsWrapper:getDestroyedBlocks(kind) or 0 end end
local VALUES = {
    birdsShot = function() return SettingsWrapper:getBirdsShot() end,
    destroyedBlocks = function() return SettingsWrapper:getAllDestroyedBlocks() end,
    destroyedBubble = blocks('bubble'), destroyedPig = blocks('pig'), destroyedGlass = blocks('glass'),
    destroyedWood = blocks('wood'), destroyedStone = blocks('stone'), destroyedPopcornSeed = blocks('popcorn_seed'),
    destroyedAsteroid = blocks('burningAsteroid'), crystalsExploded = blocks('crystalPlanet'),
    frozenPigs = function() return SettingsWrapper:getFrozenPigs() end,
    birdsExitedGravity = function() return SettingsWrapper:getBirdsExitedGravity() end,
    mainMenuObjectsPopped = function() return SettingsWrapper:getPoppedMainMenuItems() end,
    waterJumps = function() return settings.root.birdWaterJumpAmount or 0 end,
    totalStars = all_stars,
    unlockedBonusLevels = function() return (eggsteroids()) end,
}
local COUNTS = {FEATHER_PICKER = function() return (feathers()) end}
-- Episode titles are images; their English names, like the achievement names.
local EPISODES = {theme1 = 'Pig Bang', theme2 = 'Cold Cuts', theme4 = 'Utopia', theme5 = 'Red Planet',
                  theme6 = 'Pig Dipper', theme7 = 'Cosmic Crystals', theme81 = 'Beak Impact 1',
                  theme82 = 'Beak Impact 2', theme9 = 'Brass Hogs', dangerZone = 'Danger Zone'}
local PLAYTIME = {SPACE_BIRD_FAN = true, TRUE_SPACE_BIRD_FAN = true, SPACE_BIRD_ADDICT = true}
local ORDER = {
    'PROFESSIONAL_LEVEL', 'STARMAN', 'COSMIC_DISCOVERY', 'EGGSTEROID_HUNTER', 'COSMIC_FEATHER', 'FEATHER_PICKER',
    'VEGETABLES_OF_VENGEANCE', 'HANDY_MAN', 'THE_BIG_BURP', 'CURIOSITY_KILLED_THE_PIG', 'SUBTASTIC',
    'SHATTERED_DREAMS', 'WHERE_NO_BIRDS_HAVE_GONE_BEFORE', 'BIRDS_OF_COURAGE',
    'EPISODE_1_TOTAL_DESTRUCTION', 'EPISODE_2_TOTAL_DESTRUCTION', 'EPISODE_4_TOTAL_DESTRUCTION',
    'EPISODE_5_TOTAL_DESTRUCTION', 'EPISODE_6_TOTAL_DESTRUCTION', 'EPISODE_7_TOTAL_DESTRUCTION',
    'EPISODE_81_TOTAL_DESTRUCTION', 'EPISODE_82_TOTAL_DESTRUCTION', 'EPISODE_9_TOTAL_DESTRUCTION',
    'DANGER_ZONE_1_TOTAL_DESTRUCTION',
    'EPISODE_1_SCORE_ADDICT', 'EPISODE_2_SCORE_ADDICT', 'EPISODE_4_SCORE_ADDICT', 'EPISODE_5_SCORE_ADDICT',
    'EPISODE_6_SCORE_ADDICT', 'EPISODE_7_SCORE_ADDICT', 'EPISODE_81_SCORE_ADDICT', 'EPISODE_82_SCORE_ADDICT',
    'EPISODE_9_SCORE_ADDICT', 'DANGER_ZONE_1_SCORE_ADDICT',
    'ABSOLUTE_ZERO', 'BANGING_HEADS', 'BIG_SPOOK', 'TRIPLE_FREEZE', 'CENTRIPETAL_FORCE', 'GRAVITATIONAL_ESCAPE',
    'WATER_JUMPER', 'BIRD_LAUNCHER', 'PIG_POPPER', 'PIGSICLE', 'BUBBLE_POPPER', 'POPCORN_ADDICT',
    'GREAT_BALLS_OF_FIRE', 'THROW_CRYSTALS_TO_THE_PIG', 'WOODPECKER', 'ICEPICKER', 'STONECUTTER',
    'BLOCK_SMASHER', 'SMASH_MANIAC', 'MENU_POPPER', 'NAME_POPPER', 'FOWL_DELIVERY',
    'SPACE_BIRD_FAN', 'TRUE_SPACE_BIRD_FAN', 'SPACE_BIRD_ADDICT',
}
-- Bought in the game's shop (offline-shop awards them), or in Danger Zone.
local function danger_zone() return reachable('dangerZone') end
local SHOP = {
    WHERE_NO_BIRDS_HAVE_GONE_BEFORE = function() return offlineShopUnlimitedStars ~= nil end,
    FOWL_DELIVERY = function() return offlineShopUnlimitedStars ~= nil end,
    BIRDS_OF_COURAGE = danger_zone, DANGER_ZONE_1_TOTAL_DESTRUCTION = danger_zone,
    DANGER_ZONE_1_SCORE_ADDICT = danger_zone,
}
local function achievements()
    local list = {}
    for _, key in ipairs(ORDER) do
        local a = GameAchievements[key]
        if a and (not SHOP[key] or SHOP[key]()) then list[#list + 1] = {key = key, data = a} end
    end
    return list
end
local function unlocked(a) return SettingsWrapper:hasAchievement(a.data.name) end

-- Name, description and progress (value, target or nil).
local function describe(a)
    local key, data = a.key, a.data
    local limit = data.limit
    if PLAYTIME[key] then
        local hours = math.floor(limit / 3600)
        return data.name, description(key):format(number(hours)), math.floor(SettingsWrapper:getPlaytime() / 3600), hours
    end
    local index = episode(key)
    if index then
        local score, s, _, _, _, power = calculateEpisodeScoreAndStars(index)
        -- "Space Episode 1 - Score Addict" -> "Pig Bang - Score Addict"
        local name = EPISODES[actions.myLevelFolders[index]] .. ' - ' .. data.name:match(' %- (.*)$')
        if key:find('SCORE') then
            return name, description('score'):format(number(limit)), math.max(score, power), limit
        end
        return name, description('stars'):format(number(limit)), s, limit
    end
    local value = COUNTS[key] or VALUES[data.stat or '']
    local d = description(key) or ''
    return data.name, d:find('%%s') and d:format(number(limit or 0)) or d, value and value(), limit
end
function gameStatisticsAchievementName(achievement)
    for key, data in pairs(GameAchievements) do
        if data == achievement then return (describe({key = key, data = data})) end
    end
    return achievement.name
end

local function parts()
    local list = {}
    local s, smax = stars()
    list[#list + 1] = {text('stars'), s, smax}
    local f, fmax = feathers()
    list[#list + 1] = {text('feathers'), f, fmax}
    local e, emax = eggsteroids()
    list[#list + 1] = {text('eggsteroids'), e, emax}
    local all, count = achievements(), 0
    for _, a in ipairs(all) do if unlocked(a) then count = count + 1 end end
    list[#list + 1] = {text('achievements'), count, #all}
    return list
end
local function counters()
    local done, total, score = levels_and_score()
    local w = SettingsWrapper
    return {
        {text('levels'), number(done) .. ' / ' .. number(total)},
        {text('score'), number(score)},
        {text('playtime'), statistics.playtime(w:getPlaytime(), language())},
        {text('birds'), number(w:getBirdsShot())},
        {text('pigs'), number(w:getDestroyedBlocks('pig'))},
        {text('wood'), number(w:getDestroyedBlocks('wood'))},
        {text('ice'), number(w:getDestroyedBlocks('glass'))},
        {text('stone'), number(w:getDestroyedBlocks('stone'))},
        {text('frozen'), number(w:getFrozenPigs())},
        {text('missions'), number(w:getMissionsDone())},
    }
end

-- The page, in screen pixels.
local PER_PAGE = 6
-- Children need unique names.
local serial = 0
local function unique(name)
    serial = serial + 1
    return name .. serial
end
-- Sizes are for a height of 720 (statistics.set_factor). Measured there: 'MMMMMMMMMM'
-- in the small font is 192 wide, BTN_ACHIEVEMENTS 82.
local text_k, sprite_k = 1, 1
local function measure_sets()
    local t = ui.Text:new(unique('measure'), 'MMMMMMMMMM', nil, getFontSmall, 'LEFT', 'TOP')
    local ok, w = pcall(res.getSpriteBounds, 'BTN_ACHIEVEMENTS')
    text_k = statistics.set_factor(t.w, 192)
    sprite_k = statistics.set_factor(ok and w, 82)
end
local function label(parent, s, x, y, anchor, font, max_width)
    local t = ui.Text:new(unique('label'), tostring(s), nil, font or getFontSmall, anchor or 'LEFT', 'TOP')
    local k = text_k * screenHeight / 720
    if max_width and (t.w or 0) * k > max_width then k = max_width / t.w end
    t:setScale(k)
    t.x, t.y = x, y
    parent:addChild(t)
    return t
end
local function rect(parent, x, y, w, h, r, g, b, a)
    local box = ui.Rectangle:new(unique('rect'))
    box.x, box.y, box.w, box.h = x, y, w, h
    box.r, box.g, box.b, box.a = r, g, b, a or 1
    parent:addChild(box)
    return box
end
local function image(parent, sprite, x, y, scale)
    local i = ui.Image:new(unique('image'))
    i:setImage(sprite)
    i.x, i.y = x, y
    scale = (scale or 1) * sprite_k
    i.scaleX, i.scaleY = scale, scale
    parent:addChild(i)
    return i
end
local function button(parent, name, sprite, x, y, scale)
    local b = ui.ScalableButton:new(name)
    b:setImage(sprite)
    b.x, b.y = x, y
    scale = (scale or 1) * sprite_k
    -- A button returns to minScale after a press.
    b.scaleX, b.scaleY, b.minScale, b.maxScale = scale, scale, scale, scale * 1.1
    b.returnValue = name
    parent:addChild(b)
    return b
end

GameStatisticsPage = lua.Class.create(ui.Frame)

function GameStatisticsPage:init()
    ui.Frame.init(self, 'gameStatisticsPage')
    measure_sets()
    local W, H = screenWidth, screenHeight
    self.x, self.y, self.w, self.h = 0, 0, W, H
    rect(self, 0, 0, W, H, 0, 0, 0, 0.6)
    local box = ui.BGBox:new('box')
    box.suffix = 'ACHIEVEMENTS'
    box:setSprites()
    box.x, box.y = W / 2, H / 2
    box:setSize(W * 0.86, H * 0.86)
    self:addChild(box)
    local s = ReferenceScaler.getWorldScale(true)
    label(self, text('title'), W / 2, H * 0.05, 'HCENTER', getFontHeadline)
    button(self, 'STATISTICS_CLOSE', 'BTN_X', W * 0.94, H * 0.08, s * 0.8)

    -- left: the progress and the counters
    local left, right = W * 0.07, W * 0.47
    local list = parts()
    local total = statistics.progress(list)
    local y = H * 0.17
    label(self, text('progress'), left, y, 'LEFT', getFontBasic)
    label(self, string.format('%d %%', math.floor(total * 100)), right, y, 'RIGHT', getFontBasic)
    y = y + 44 * s
    rect(self, left, y, right - left, 22 * s, 0, 0, 0, 0.35)
    rect(self, left + 3 * s, y + 3 * s, math.max((right - left - 6 * s) * total, 0), 16 * s, 1, 0.8, 0.1)
    y = y + 40 * s
    local row = 30 * s
    for _, p in ipairs(list) do
        label(self, p[1], left, y)
        label(self, number(p[2]) .. ' / ' .. number(p[3]), right, y, 'RIGHT')
        y = y + row
    end
    y = y + 12 * s
    for _, c in ipairs(counters()) do
        label(self, c[1], left, y)
        label(self, c[2], right, y, 'RIGHT')
        y = y + row
    end

    -- right: the achievements, a page at a time
    self.list = achievements()
    local count = 0
    for _, a in ipairs(self.list) do if unlocked(a) then count = count + 1 end end
    self.area = {x0 = W * 0.53, x1 = W * 0.93, y = H * 0.17}
    label(self, text('achievements'), self.area.x0, self.area.y, 'LEFT', getFontBasic)
    label(self, number(count) .. ' / ' .. number(#self.list), self.area.x1, self.area.y, 'RIGHT', getFontBasic)
    self.pages = math.ceil(#self.list / PER_PAGE)
    local by = H * 0.86
    self.back = button(self, 'STATISTICS_BACK', 'BTN_ARROW_LEFT', self.area.x0 + 30 * s, by, 0.6 * s)
    self.forward = button(self, 'STATISTICS_FORWARD', 'BTN_ARROW_RIGHT', self.area.x1 - 30 * s, by, 0.6 * s)
    self.pageText = label(self, '', (self.area.x0 + self.area.x1) / 2, by - 12 * s, 'HCENTER')
    self:showPage(1)
end

function GameStatisticsPage:showPage(page)
    page = math.max(1, math.min(page, self.pages))
    self.page = page
    if self.rows then self:removeChild(self.rows) end
    local rows = ui.Frame:new('rows')
    self.rows = rows
    self:addChild(rows)
    local s = ReferenceScaler.getWorldScale(true)
    local area = self.area
    local y = area.y + 50 * s
    for i = (page - 1) * PER_PAGE + 1, math.min(page * PER_PAGE, #self.list) do
        local a = self.list[i]
        local done = unlocked(a)
        local name, info, value, target = describe(a)
        image(rows, done and 'BTN_ACHIEVEMENTS' or 'BTN_ACHIEVEMENTS_DISABLED', area.x0 + 24 * s, y + 26 * s, 0.55 * s)
        local status
        if done then
            status = text('unlocked')
        elseif value and target then
            status = number(math.min(value, target)) .. ' / ' .. number(target)
        end
        local status_w = 0
        if status then
            local t = label(rows, status, area.x1, y + 4 * s, 'RIGHT')
            status_w = (t.w or 0) * t.scaleX + 12 * s
        end
        label(rows, name, area.x0 + 56 * s, y, 'LEFT', getFontBasic, area.x1 - area.x0 - 56 * s - status_w)
        label(rows, info, area.x0 + 56 * s, y + 30 * s, 'LEFT', getFontSmall, area.x1 - area.x0 - 56 * s)
        y = y + 64 * s
    end
    self.pageText:setText(page .. ' / ' .. self.pages)
    self.back.visible = page > 1
    self.forward.visible = page < self.pages
end

function GameStatisticsPage:onPointerEvent(...)
    local result = ui.Frame.onPointerEvent(self, ...)
    if result == 'STATISTICS_CLOSE' then
        GameSystem.eventManager:notify({id = events.EID_POP_FRAME})
    elseif result == 'STATISTICS_BACK' then
        self:showPage(self.page - 1)
    elseif result == 'STATISTICS_FORWARD' then
        self:showPage(self.page + 1)
    end
    return 'BLOCK'
end

function GameStatisticsPage:onKeyEvent(event, key)
    if event == 'PRESS' and (key == 'KEY_BACK' or key == 'ESCAPE') then
        GameSystem.eventManager:notify({id = events.EID_POP_FRAME})
    end
    return 'BLOCK'
end

-- Unlock banner, top centre: slides in, stays BANNER_TIME seconds, queued, not blocking.
local BANNER_TIME, BANNER_SLIDE = 4, 0.3
local Banner = lua.Class.create(ui.Frame)
function Banner:init(achievement)
    ui.Frame.init(self, unique('gameStatisticsBanner'))
    measure_sets()
    local s = ReferenceScaler.getWorldScale(true)
    local name = gameStatisticsAchievementName(achievement)
    local caption = label(self, text('banner'), 0, 0, 'LEFT', getFontSmall)
    local title = label(self, name, 0, 0, 'LEFT', getFontBasic, screenWidth * 0.5)
    local icon_w = 60 * s
    local w = icon_w + math.max(caption.w * caption.scaleX, title.w * title.scaleX) + 50 * s
    local h = 86 * s
    local box = ui.BGBox:new(unique('box'))
    box.suffix = 'ACHIEVEMENTS'
    box:setSprites()
    box:setSize(w, h)
    self:addChild(box)
    self:removeChild(caption)
    self:removeChild(title)
    self:addChild(caption)
    self:addChild(title)
    image(self, 'BTN_ACHIEVEMENTS', -w / 2 + 20 * s + icon_w / 2, 0, 0.62 * s)
    caption.x, caption.y = -w / 2 + 25 * s + icon_w, -h / 2 + 12 * s
    title.x, title.y = caption.x, caption.y + 26 * s
    self.h = h
    self.time = 0
    self.x, self.y = screenWidth / 2, -h
end
function Banner:update(dt, ...)
    local results = {ui.Frame.update(self, dt, ...)}
    self.time = self.time + (dt or 0)
    local t = self.time
    local shown = math.min(t / BANNER_SLIDE, 1, math.max((BANNER_TIME + 2 * BANNER_SLIDE - t) / BANNER_SLIDE, 0))
    self.y = -self.h / 2 + (self.h + 24 * ReferenceScaler.getWorldScale(true)) * shown
    if t >= BANNER_TIME + 2 * BANNER_SLIDE then
        GameSystem.menuManager:notificationsFrame():removeChild(self)
        table.remove(gameStatisticsBanners, 1)
        if gameStatisticsBanners[1] then
            GameSystem.menuManager:notificationsFrame():addChild(Banner:new(gameStatisticsBanners[1]))
        end
    end
    return unpack(results)
end
function Banner:onPointerEvent() end
gameStatisticsBanners = gameStatisticsBanners or {}
function gameStatisticsBanner(achievement)
    table.insert(gameStatisticsBanners, achievement)
    if #gameStatisticsBanners == 1 then
        GameSystem.menuManager:notificationsFrame():addChild(Banner:new(achievement))
    end
end

-- The button, top left (the options button is bottom left).
local init = menu.MainMenu.init
function menu.MainMenu:init(...)
    local results = {init(self, ...)}
    local b = ui.ScalableButton:new('statisticsButton')
    b:setImage('BTN_ACHIEVEMENTS')
    b.returnValue = 'SHOW_STATISTICS'
    -- The menu ignores return values it does not know; the button opens the page.
    function b:onPointerEvent(...)
        local results = {ui.ScalableButton.onPointerEvent(self, ...)}
        if results[1] == 'SHOW_STATISTICS' then
            GameSystem.eventManager:notify({id = events.EID_PUSH_FRAME, target = GameStatisticsPage:new()})
        end
        return unpack(results)
    end
    self:addChild(b)
    return unpack(results)
end
local layout = menu.MainMenu.layout
function menu.MainMenu:layout(...)
    local results = {layout(self, ...)}
    local b, options = self:getChild('statisticsButton'), self:getChild('optionsButton')
    if b and options then
        statistics.match_button(b, options)
        b.x, b.y = options.x, screenHeight - options.y
    end
    return unpack(results)
end
)";

class GameStatistics final : public slingshot::Mod {
public:
    GameStatistics() {
        using slingshot::mods::append_lua;
        append_lua("data/scripts/AchievementsLogic.lua", kBanner);
        append_lua("data/scripts/menu/MainMenu.lua", std::string(slingshot::mods::kStatisticsLua) + kPage);
    }
};

}  // namespace

SLINGSHOT_MOD(GameStatistics, "game-statistics",
              "Statistics, progress and achievements on a page of the main menu; unlocks show as a banner");
