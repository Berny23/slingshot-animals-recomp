// SPDX-License-Identifier: GPL-3.0-or-later
// A main menu button opens a page with the progress to 100 %, the game's counters
// and its achievements; unlocks show as the game's banner.
//
// On Android the game has no achievement table, so registerAchievement stored
// nothing. Here the achievements that can be unlocked offline are stored and
// shown; those met before are registered when the page opens. Not listed:
// "Fowl Delivery" (never awarded), "Laser Shooter Trainee" (the free version's)
// and "Popcorn Popper" (no target). Descriptions follow the console versions.
// The game's texts are English only.
//
// The progress is the mean of stars, Mighty Falcon feathers, droid levels and
// achievements.
#include <string>

#include "common/game_scripts.hpp"
#include "common/statistics.hpp"
#include "mod.hpp"

namespace {

constexpr const char* kAchievements = R"(
-- The achievements that can be unlocked offline, in the page's order.
-- progress: value and target of a running count.
local function count(getter, ...)
    local args = {...}
    return function()
        local ok, value = pcall(SettingsWrapper[getter], SettingsWrapper, unpack(args))
        return ok and tonumber(value) or 0
    end
end
local function episode(type_index, folder_index)
    return function()
        local score, stars, max_stars, _, _, power = calculateEpisodeScoreAndStars(type_index, folder_index)
        return score, stars, max_stars, power
    end
end
local list = {}
-- earned: true when the saved progress meets it (a running count meets its target).
local function add(name, description, progress, earned)
    list[#list + 1] = {name = name, description = description, progress = progress, earned = earned}
end
local function at_least(getter, n)
    local value = count(getter)
    return function() return value() >= n end
end
-- Levels whose completion is an achievement (levelAchievement).
local function completed(name)
    return function()
        for level, meta in pairs(actions.myLevelsMetadata) do
            if meta.levelAchievement == name and highscores[level] then return true end
        end
        return false
    end
end
-- Each episode: its last level, all stars, the score.
local EPISODES = {
    {'Tatooine', 1, 1, "That's No Moon"},
    {'Death Star', 1, 2, 'Disturbing Misobedience'},
    {'Hoth', 1, 3, 'Transport Is Away'},
    {'Cloud City', 1, 4, 'Meet you at the rendezvous'},
    {'Dagobah', 2, 1, 'Courage Of Jedi'},
    {'Moon of Endor', 1, 5, 'The shield is down'},
    {'Death Star 2', 1, 6, 'Smash the Empire'},
}
for _, e in ipairs(EPISODES) do
    local title, data = e[1], episode(e[2], e[3])
    add(e[4], title .. ' completed', nil, completed(e[4]))
    add(title .. ' - War for the Stars', title .. ': Three stars in all levels', function()
        local _, stars, max_stars = data()
        return stars, max_stars
    end)
    local power = title .. ' - Power of the Force'
    add(power, title .. ': Get %s points', function()
        local score, _, _, power_score = data()
        return math.max(score or 0, power_score or 0), getAchievementLimit(power)
    end)
end
add('Dangerous It Will Be', 'Path of the Jedi unlocked', nil, function()
    return SettingsWrapper:isEpisodePurchased(getEpisodeMetadataWithParam('dagobah_1', 'iapPurchaseId'))
end)
add('Mighty Launch', 'Mighty Falcon launched', nil, at_least('getPlayerUsedEaglesCount', 1))
add('Falcon Escape', '100% destruction with a Mighty Falcon', nil, at_least('getFalconTotalDestructionCount', 1))
add('Hyper Space', '100% destruction with a Mighty Falcon %s times', count('getFalconTotalDestructionCount'))
add('Probe Explorer', '1 droid level found', nil, at_least('getNumDroidLevelUnlockedThroughEggsteroid', 1))
add('Probe Gatherer', '%s droid levels found', count('getNumDroidLevelUnlockedThroughEggsteroid'))
add('Force Learner', '%s blocks thrown with Force', count('getNumBlocksThrownByForce'))
add('Ultimate Laser Shooter', '%s blocks smashed with blaster laser', count('getNumBlocksDestroyedByPlayerLaser'))
add('Skilled Pilot', '%s blocks smashed with Pilot', count('getNumBlocksDestroyedByBird', 'materialBlueBird'))
add('Magnificent Grumble', '%s blocks smashed with Chewbacca', count('getNumBlocksDestroyedByBird', 'materialBigBird'))
add('Jedi Initiate', '%s blocks smashed with saber', count('getNumBlocksDestroyedBySword'))
add('Jedi Knight', '%s blocks smashed with saber', count('getNumBlocksDestroyedBySword'))
add('Jedi Master', '%s blocks smashed with saber', count('getNumBlocksDestroyedBySword'))
add('Prominent Jedi', 'Three lasers deflected with saber')
add('Target Shooter', 'Three pigs shot with a single blaster shot')
add('Imperial Entanglement', "Stormtrooper popped with Tie Fighter's wing")
add('Walking Carpet', 'Same turret gets direct hit on Chewbacca %s times')
add('Fatal Attraction', '%s blocks lifted with a gravitational field disruptor', count('getNumBlocksLiftedByLeia'))
add('Mynock Tangle', 'Mynock popped with another Mynock in %s levels', count('getNumberOfMynocksCollided'))
add('Block Buster', '%s blocks smashed with a single flight of Boba')
add('Banging Heads', 'Pig popped with another pig')
add('Call the ewoks', 'Drums hit %s times in one level')
add('Beat the Emperor', 'Lightning used %s times in the Emperor battle')
add('Simple Pigs and Nonsense', '%s Stormtroopers popped', count('getDestroyedBlocks', 'stormTrooper'))
add('The Birds Will Be With You', '%s levels completed with one bird', count('getTimesLevelFinishedUsingOneBird'))
add('Pork side of the Force', 'Failed level %s times with one pig left', count('getTimesFailedWithOnePigLeft'))
add('Lack of Faith', 'Level restarted %s times', count('getTimesLevelRestarted'))
add('Fly Bird Fly', '%s birds launched', count('getBirdsShot'))
add('Star Bird Fan', 'Played for %s hours', count('getPlaytime'))
add('True Star Bird Fan', 'Played for %s hours', count('getPlaytime'))
add('Star Bird Addict', 'Played for %s hours', count('getPlaytime'))
gameStatisticsAchievements = list

local BY_NAME = {}
for _, a in ipairs(list) do BY_NAME[a.name] = a end
local HOURS = {['Star Bird Fan'] = true, ['True Star Bird Fan'] = true, ['Star Bird Addict'] = true}
local function number(n)
    local s = string.format('%.0f', math.floor(n or 0))
    return (s:reverse():gsub('(%d%d%d)', '%1,'):reverse():gsub('^,', ''))
end
-- Description, value and target (nil without a running count).
function gameStatisticsDescribe(a)
    local limit = currentAchievementLimits and currentAchievementLimits[a.name]
    local value, target
    if a.progress then value, target = a.progress() end
    target = target or limit
    if HOURS[a.name] and limit then
        limit = limit / 3600
        value, target = math.floor((value or 0) / 3600), limit
    end
    local description = a.description
    description = description:gsub('%%s', function() return number(limit) end)
    if not a.progress then value, target = nil, nil end
    return description, value, target
end

-- Android has no achievement table, so registerAchievement stored nothing;
-- here it stores and announces the offline ones.
gameStatisticsBanners = gameStatisticsBanners or {}
function registerAchievement(name)
    if not BY_NAME[name] or SettingsWrapper:hasAchievement(name) then return end
    SettingsWrapper:registerAchievement(name)
    table.insert(gameStatisticsBanners, name)
end
-- Achievements met before (Android stored none), registered without a banner.
function gameStatisticsCatchUp()
    for _, a in ipairs(list) do
        if not SettingsWrapper:hasAchievement(a.name) then
            local ok, earned
            if a.earned then
                ok, earned = pcall(a.earned)
            elseif a.progress then
                ok, earned = pcall(function()
                    local _, value, target = gameStatisticsDescribe(a)
                    return value and target and value >= target
                end)
            end
            if ok and earned then SettingsWrapper:registerAchievement(a.name) end
        end
    end
end

-- The game's banner (bottom right), one at a time.
local frame_update = achievementsFrameUpdate
function achievementsFrameUpdate(...)
    if gameStatisticsBanners[1] and time - (gameStatisticsBannerTime or -100) > achievementPopUpDuration
        and notificationsFrame and BannerPopup then
        local a = BY_NAME[table.remove(gameStatisticsBanners, 1)]
        gameStatisticsBannerTime = time
        pcall(function()
            local popup = BannerPopup:new(a.name, (gameStatisticsDescribe(a)), 'BTN_ACHIEVEMENTS')
            popup.name = 'achievement'
            notificationsFrame:addChild(popup)
            popup:onEntry()
            popup:layout()
        end)
    end
    return frame_update(...)
end
)";

// The menu scripts reach the standard library through _G.
constexpr const char* kLocals = R"(
local tostring, tonumber, type, pairs, ipairs, pcall, unpack = _G.tostring, _G.tonumber, _G.type, _G.pairs, _G.ipairs, _G.pcall, _G.unpack
local string, math, table = _G.string, _G.math, _G.table
local g = gamelua
)";

constexpr const char* kPage = R"(
local TEXTS = {
    title = 'Statistics', progress = 'Game progress', stars = 'Stars', feathers = 'Mighty Falcon feathers',
    droids = 'Droid levels', achievements = 'Achievements', levels = 'Levels completed',
    score = 'Total score', playtime = 'Playtime', birds = 'Birds launched', stormtroopers = 'Stormtroopers popped',
    falcons = 'Mighty Falcons launched', restarts = 'Levels restarted', unlocked = 'Unlocked',
}
local function text(key) return TEXTS[key] end
local function number(n) return statistics.number(n, 'en') end

-- Episodes by type (actions.episodeTypeTable); the last type is the next
-- episode's teaser.
local function episodes()
    local list = {}
    local types = g.actions.episodeTypeTable
    for t = 1, #types - 1 do
        for f, folder in ipairs(g.actions.myLevelFolders[types[t]] or {}) do
            list[#list + 1] = {type = t, index = f, folder = folder, meta = g.actions.myEpisodesMetadata[folder] or {}}
        end
    end
    return list
end
-- calculateEpisodeScoreAndStars: score, stars, max stars, feathers, max feathers, power-up score.
local function stars()
    local done, total = 0, 0
    for _, e in ipairs(episodes()) do
        local _, s, max = g.calculateEpisodeScoreAndStars(e.type, e.index)
        done, total = done + s, total + max
    end
    return done, total
end
-- A feather is a Mighty Falcon score of 100 % (not in droid levels).
local function feathers()
    local done, total = 0, 0
    for _, e in ipairs(episodes()) do
        if not e.meta.hideMEButton then
            local _, _, _, d, max = g.calculateEpisodeScoreAndStars(e.type, e.index)
            done, total = done + d, total + max
        end
    end
    return done, total
end
local function playable(level)
    local meta = g.actions.myLevelsMetadata[level]
    return not (meta and (meta.type or meta.ignoreScore))
end
local function droids()
    local done, total = 0, 0
    for _, level in ipairs(g.actions.myLevels.bonus or {}) do
        if playable(level) then
            total = total + 1
            if SettingsWrapper:isDroidLevelUnlocked(level) then done = done + 1 end
        end
    end
    return done, total
end
local function levels_and_score()
    local done, total, score = 0, 0, 0
    for _, e in ipairs(episodes()) do
        for _, level in ipairs(g.actions.myLevels[e.folder] or {}) do
            if playable(level) then
                total = total + 1
                local h = g.highscores[level]
                if h and math.max(h.score or 0, h.powerUpScore or 0) > 0 then done = done + 1 end
            end
        end
        score = score + (g.calculateEpisodeScoreAndStars(e.type, e.index))
    end
    return done, total, score
end
local function unlocked(a) return SettingsWrapper:hasAchievement(a.name) end
local function get(getter, ...)
    local ok, value = pcall(SettingsWrapper[getter], SettingsWrapper, ...)
    return ok and tonumber(value) or 0
end

local function parts()
    local list = {}
    local s, smax = stars()
    list[#list + 1] = {text('stars'), s, smax}
    local f, fmax = feathers()
    list[#list + 1] = {text('feathers'), f, fmax}
    local d, dmax = droids()
    list[#list + 1] = {text('droids'), d, dmax}
    local all, count = g.gameStatisticsAchievements, 0
    for _, a in ipairs(all) do if unlocked(a) then count = count + 1 end end
    list[#list + 1] = {text('achievements'), count, #all}
    return list
end
local function counters()
    local done, total, score = levels_and_score()
    return {
        {text('levels'), number(done) .. ' / ' .. number(total)},
        {text('score'), number(score)},
        {text('playtime'), statistics.playtime(get('getPlaytime'), 'en')},
        {text('birds'), number(get('getBirdsShot'))},
        {text('stormtroopers'), number(get('getDestroyedBlocks', 'stormTrooper'))},
        {text('falcons'), number(get('getPlayerUsedEaglesCount'))},
        {text('restarts'), number(get('getTimesLevelRestarted'))},
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
local function label(parent, s, x, y, anchor, font, max_width, scale)
    local t = ui.Text:new({name = unique('label'), group = '', text = tostring(s), font = font or g.getFontBasic,
                           hanchor = anchor or 'LEFT', vanchor = 'TOP'})
    scale = scale or 1
    t:setScale(scale)
    local w = t:getWidth() * scale
    if max_width and w > max_width then t:setScale(scale * max_width / w) end
    t.x, t.y = x, y
    parent:addChild(t)
    return t
end
local function rect(parent, x, y, w, h, r, gr, b, a)
    local box = ui.Rectangle:new({name = unique('rect'), width = w, height = h})
    box.x, box.y = x, y
    box.r, box.g, box.b, box.a = r, gr, b, a or 1
    parent:addChild(box)
    return box
end
local function image(parent, sprite, x, y, scale)
    local i = ui.Image:new({name = unique('image')})
    i:setImage(sprite)
    i.x, i.y = x, y
    i.scaleX, i.scaleY = scale or 1, scale or 1
    parent:addChild(i)
    return i
end
local function button(parent, value, sprite, x, y, scale)
    local b = ui.ScallableButton:new()
    b.name = unique('button')
    b:setImage(sprite)
    b.x, b.y = x, y
    b.scaleX, b.scaleY = scale or 1, scale or 1
    b.returnValue = value
    parent:addChild(b)
    return b
end

GameStatisticsPage = g.ui.Frame:inherit()

function GameStatisticsPage:init()
    self.name = 'gameStatisticsPage'
    pcall(g.gameStatisticsCatchUp)
    self.requireInput = true
    local W, H = g.screenWidth, g.screenHeight
    local s = H / 720
    rect(self, 0, 0, W, H, 0, 0, 0, 0.6)
    local box = ui.BGBox:new({name = 'box', suffix = 'ACHIEVEMENTS'})
    box:setSize(W * 0.9, H * 0.84)
    box.x, box.y = W / 2, H * 0.54
    self:addChild(box)
    label(self, text('title'), W / 2, H * 0.03, 'HCENTER', g.getFontHeadline)
    button(self, 'STATISTICS_CLOSE', 'BTN_X', W * 0.94, H * 0.13, s * 0.8)

    -- left: the progress and the counters
    local left, right = W * 0.08, W * 0.47
    local list = parts()
    local total = statistics.progress(list)
    local y = H * 0.17
    label(self, text('progress'), left, y, 'LEFT', g.getFontBasic, nil, 1.15)
    label(self, string.format('%d %%', math.floor(total * 100)), right, y, 'RIGHT', g.getFontBasic, nil, 1.15)
    y = y + 44 * s
    rect(self, left, y, right - left, 22 * s, 0, 0, 0, 0.6)
    rect(self, left + 3 * s, y + 3 * s, math.max((right - left - 6 * s) * total, 0), 16 * s, 1, 0.8, 0.1)
    y = y + 40 * s
    local row = 32 * s
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
    self.list = g.gameStatisticsAchievements
    local count = 0
    for _, a in ipairs(self.list) do if unlocked(a) then count = count + 1 end end
    self.area = {x0 = W * 0.53, x1 = W * 0.92, y = H * 0.17}
    label(self, text('achievements'), self.area.x0, self.area.y, 'LEFT', g.getFontBasic, nil, 1.15)
    label(self, number(count) .. ' / ' .. number(#self.list), self.area.x1, self.area.y, 'RIGHT', g.getFontBasic, nil, 1.15)
    self.pages = math.ceil(#self.list / PER_PAGE)
    local by = H * 0.89
    self.back = button(self, 'STATISTICS_BACK', 'BTN_ARROW_LEFT', self.area.x0 + 30 * s, by, 0.6 * s)
    self.forward = button(self, 'STATISTICS_FORWARD', 'BTN_ARROW_RIGHT', self.area.x1 - 30 * s, by, 0.6 * s)
    self.pageText = label(self, '', (self.area.x0 + self.area.x1) / 2, by - 12 * s, 'HCENTER')
    self:showPage(1)
end

function GameStatisticsPage:showPage(page)
    page = math.max(1, math.min(page, self.pages))
    self.page = page
    if self.rows then self:removeChild(self.rows) end
    local rows = ui.Frame:new({name = unique('rows')})
    self.rows = rows
    self:addChild(rows)
    local s = g.screenHeight / 720
    local area = self.area
    local y = area.y + 50 * s
    for i = (page - 1) * PER_PAGE + 1, math.min(page * PER_PAGE, #self.list) do
        local a = self.list[i]
        local done = unlocked(a)
        local info, value, target = g.gameStatisticsDescribe(a)
        image(rows, done and 'BTN_ACHIEVEMENTS' or 'BTN_ACHIEVEMENTS_DISABLED', area.x0 + 24 * s, y + 26 * s, 0.6 * s)
        local status
        if done then
            status = text('unlocked')
        elseif value and target then
            status = number(math.min(value, target)) .. ' / ' .. number(target)
        end
        -- The name on its own line; the status beside the description.
        local status_w = 0
        if status then
            local t = label(rows, status, area.x1, y + 30 * s, 'RIGHT', g.getFontBasic, nil, 0.75)
            status_w = t:getWidth() * t.scaleX + 12 * s
        end
        label(rows, a.name, area.x0 + 56 * s, y, 'LEFT', g.getFontBasic, area.x1 - area.x0 - 56 * s)
        label(rows, info, area.x0 + 56 * s, y + 30 * s, 'LEFT', g.getFontBasic, area.x1 - area.x0 - 56 * s - status_w, 0.75)
        y = y + 64 * s
    end
    self.pageText.text = page .. ' / ' .. self.pages
    self.pageText:clip()
    self.back.visible = page > 1
    self.forward.visible = page < self.pages
end

function GameStatisticsPage:onPointerEvent(...)
    local result = g.ui.Frame.onPointerEvent(self, ...)
    if result == 'STATISTICS_CLOSE' then
        g.eventManager:notify({id = g.events.EID_POP_FRAME})
    elseif result == 'STATISTICS_BACK' then
        self:showPage(self.page - 1)
    elseif result == 'STATISTICS_FORWARD' then
        self:showPage(self.page + 1)
    end
    return 'BLOCK'
end

function GameStatisticsPage:onKeyEvent(event, key)
    if event == 'PRESS' and (key == 'KEY_BACK' or key == 'ESCAPE') then
        g.eventManager:notify({id = g.events.EID_POP_FRAME})
    end
    return 'BLOCK'
end

-- The button, top left (the options button is bottom left), in the frame that
-- pans with the menu.
local function add_button(menu)
    local frame = menu:getChild('panningFrame')
    local options = frame and frame:getChild('optionsButton')
    if not options then return end
    local b = frame:getChild('statisticsButton')
    if not b then
        b = ui.ScallableButton:new()
        b.name = 'statisticsButton'
        b:setImage('BTN_ACHIEVEMENTS')
        b.returnValue = 'SHOW_STATISTICS'
        -- The menu ignores return values it does not know; the button opens the page.
        local pointer = b.onPointerEvent
        function b:onPointerEvent(...)
            local results = {pointer(self, ...)}
            if results[1] == 'SHOW_STATISTICS' then
                g.eventManager:notify({id = g.events.EID_PUSH_FRAME, target = GameStatisticsPage:new({})})
            end
            return unpack(results)
        end
        frame:addChild(b)
    end
    b.scaleX, b.scaleY = options.scaleX, options.scaleY
    b.x, b.y = options.x, g.screenHeight - options.y
end
local layout = MainMenu.layout
function MainMenu:layout(...)
    local results = {layout(self, ...)}
    add_button(self)
    return unpack(results)
end
)";

class GameStatistics final : public slingshot::Mod {
public:
    GameStatistics() {
        using slingshot::mods::append_lua;
        append_lua("data/scripts/achievementsLogic.lua", kAchievements);
        append_lua("data/scripts/menus/main_menu.lua", std::string(kLocals) + slingshot::mods::kStatisticsLua + kPage);
    }
};

}  // namespace

SLINGSHOT_MOD(GameStatistics, "game-statistics",
              "Statistics, progress and achievements on a page of the main menu; unlocks show as a banner");
