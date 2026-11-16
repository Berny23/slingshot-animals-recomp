// SPDX-License-Identifier: GPL-3.0-or-later
// A main menu button opens a page with the progress to 100 %, the game's counters
// and its Game Center achievements; unlocks show in the game's achievement popup.
//
// The game unlocks and stores the Game Center achievements offline
// (AchievementProcessor, settings); only the iOS Game Center showed them, and with
// it the popup (AchievementPopup, filled from gameCenter.achievements). The names
// and icons are in g_achievements; the game has no descriptions, these follow its
// conditions. The Mighty Eagle's four are listed with the Mighty Eagle
// (offline-shop or bought).
//
// The block, pig and themed-object counters (updateDestroyedBlocksValues) only
// count on iOS; here they always count.
//
// The progress is the mean of stars, golden eggs, feathers (with the Mighty
// Eagle), achievements, the journal's completion tiers and hats. The Pig Challenge
// counts with offline-tournament (its weeks with a trophy, the journal's
// Challenge tab); without it, it needs Rovio's servers. Hats count when they can
// be had: 12 come only with offline-shop, or with offline-tournament from the
// journal's last milestone. Left out: the paid Tea Party and Ham Dunk team
// levels unless offline-shop sells them or they are bought.
#include <string>

#include "common/game_scripts.hpp"
#include "common/statistics.hpp"
#include "mod.hpp"

namespace {

// The counters, as on iOS.
constexpr const char* kCounters = R"(
if not updateDestroyedBlocksValues_offline then
    local update = updateDestroyedBlocksValues
    function updateDestroyedBlocksValues(...)
        if deviceModel == 'iphone' or deviceModel == 'ipad' then return update(...) end
        local model = deviceModel
        deviceModel = 'ipad'
        local results = {pcall(update, ...)}
        deviceModel = model
        if not results[1] then error(results[2], 0) end
        return unpack(results, 2)
    end
    updateDestroyedBlocksValues_offline = true
end
)";

// The page: texts, the numbers, the page frame, the popup and the main menu's button.
constexpr const char* kPage = R"(
local TEXTS = {
    en = {
        title = 'Statistics', progress = 'Game progress', stars = 'Stars', eggs = 'Golden eggs',
        feathers = 'Mighty Eagle feathers', achievements = 'Achievements', journal = 'Journal achievements',
        trophies = 'Pig Challenge trophies', hats = 'Hats',
        levels = 'Levels completed', score = 'Total score', playtime = 'Playtime', birds = 'Birds flung',
        pigs = 'Pigs popped', wood = 'Wood blocks destroyed', ice = 'Ice blocks destroyed',
        stone = 'Stone blocks destroyed', unlocked = 'Unlocked',
        clear = 'Complete all levels of %s', clear_page = 'Complete all levels on page %d of %s',
        destruction = 'Get three stars on every level of %s', score_addict = 'Score %s points in %s',
        feather_text = 'Collect %s Mighty Eagle feathers', eagle_text = 'Get the Mighty Eagle',
        globe = 'Meet the Globe', portal = 'Send one bird through portals %s times',
        birds_text = 'Fling %s birds', pigs_text = 'Pop %s pigs', blocks_text = 'Destroy %s blocks',
        wood_text = 'Destroy %s wood blocks', ice_text = 'Destroy %s ice blocks', stone_text = 'Destroy %s stone blocks',
        pumpkin = 'Smash %s pumpkins', presents = 'Smash %s presents', snow = 'Smash %s snow blocks',
        chocolate = 'Smash %s boxes of chocolates', clouds = 'Pop %s clouds', money = 'Smash %s piles of coins',
        easteregg = 'Smash %s Easter eggs', bunny = 'Smash %s bunny ears', summerFlower = 'Smash %s flowers',
        summerPicnic = 'Smash %s picnic items', chineseLantern = 'Smash %s lanterns',
        bunnyStatue = 'Smash %s bunny statues', goldpot = 'Smash %s pots of gold', skeleton = 'Smash %s bones',
        cookie = 'Smash %s cookies', halloweenTNT = 'Blow up %s black pumpkins',
    },
}
local function language() return 'en' end  -- (the game's only language)
local text = statistics.texts(TEXTS, language)
local function number(n) return statistics.number(n, language()) end

local function processor() return achievementProcessor or {} end
local function eagle()
    local item = g_iap_item_info and g_iap_item_info.mightyeagle
    return (item and item.price and item.price.coins) ~= nil or settingsWrapper:isMightyEagleEnabled() == true
end
-- offline-shop gives the paid levels a coin price.
local function sold(id)
    local item = g_iap_item_info and g_iap_item_info[id]
    return (item and item.price and item.price.coins) ~= nil
end
local function tea_party() return sold('unlockteapartylevels') or settingsWrapper:isTeaPartyLevelsBought() == true end
local function ham_dunk() return sold('unlocknbalevels') or settingsWrapper:isNBALevelsBought() == true end

local function episodes()
    local list = {}
    for i = 1, getEpisodeCount() do list[i] = getLocalEpisode(i) end
    return list
end
local function episode_named(name)
    for i, e in ipairs(episodes()) do if e.name == name then return i, e end end
end
local function blocks()
    local w = settingsWrapper
    return w:getWoodBlocksDestroyed() + w:getIceBlocksDestroyed() + w:getRockBlocksDestroyed()
end

-- {name, describe = function: text, value, target}; eagle = listed with the Mighty Eagle
local ACHIEVEMENTS = {}
local function add(name, describe, eagle_only)
    ACHIEVEMENTS[#ACHIEVEMENTS + 1] = {name = name, describe = describe, eagle = eagle_only}
end
local function counter(key, value, limit)
    return function()
        local target = processor()[limit]
        return text(key):format(number(target or 0)), value(), target
    end
end
-- Episode pages that clear an achievement (pages[].clear_achievement).
local function cleared(name)
    return function()
        for _, e in ipairs(episodes()) do
            local pages, index = 0, nil
            for _, p in ipairs(e.pages or {}) do
                if p.clear_achievement and p.clear_achievement ~= '' then
                    pages = pages + 1
                    if p.clear_achievement == name then index = pages end
                end
            end
            if index then
                if pages == 1 then return text('clear'):format(e.name) end
                return text('clear_page'):format(index, e.name)
            end
        end
        return ''
    end
end
local LEGACY = {Halloween = 'Trick or Treat'}
local function destruction(prefix)
    return function()
        local i, e = episode_named(LEGACY[prefix] or prefix)
        if not i then return '' end
        local stars, max = calculateEpisodeStars(i)
        return text('destruction'):format(e.name), stars, max
    end
end
local function score_addict(prefix)
    return function()
        local i, e = episode_named(LEGACY[prefix] or prefix)
        if not i then return '' end
        local limit = e.score_achievement_limit or 0
        return text('score_addict'):format(number(limit), e.name), calculateEpisodeScore(i), limit
    end
end
local function material(name, limit)
    return counter(name, function() return settingsWrapper:getBlocksDestroyed(name) end, limit)
end

for _, name in ipairs({"Pork O'Lantern", 'Ominous Oinker', 'Haunted Ham', 'Holiday Spirit',
        "All's Fair in Love and War", "It's all Hunky Dory!", 'Hardboiled Egg', 'Oh, Those Summer Nights',
        'Bright Side of the Moon', 'Eggxorcist', 'Merry Happy!', 'ABRA-CA-BACON'}) do
    add(name, cleared(name))
end
for _, prefix in ipairs({'Halloween', "Season's Greedings", 'Hogs and Kisses', 'Go Green, Get Lucky',
        'Easter Eggs', 'Summer Pignic', 'Moon Festival', "Ham'o'ween", 'Wreck the Halls'}) do
    add(prefix .. ' - Total Destruction', destruction(prefix))
    add(prefix .. ' - Score Addict', score_addict(prefix))
end
add('Bird Slinger', counter('birds_text', function() return settingsWrapper:getBirdsShot() end, 'birdSlingerLimit'))
add('Pig Popper', counter('pigs_text', function() return settingsWrapper:getPigsDestroyed() end, 'pigPopperLimit'))
add('Woodpecker', counter('wood_text', function() return settingsWrapper:getWoodBlocksDestroyed() end, 'woodPeckerLimit'))
add('Ice Picker', counter('ice_text', function() return settingsWrapper:getIceBlocksDestroyed() end, 'icePickerLimit'))
add('Stone Cutter', counter('stone_text', function() return settingsWrapper:getRockBlocksDestroyed() end, 'stoneCutterLimit'))
add('Block Smasher', counter('blocks_text', blocks, 'blockSmasherLimit'))
add('Smash Maniac', counter('blocks_text', blocks, 'smashManiacLimit'))
add('Pumpkin Smasher', material('pumpkin', 'pumpkinSmasherLimit'))
add('Black is the New Black', material('halloweenTNT', 'blackIsTheNewBlackLimit'))
add('Lovely Bones', material('skeleton', 'lovelyBonesLimit'))
add('Unwrapper', material('presents', 'unwrapperLimit'))
add('The Grinch', material('presents', 'grinchLimit'))
add('Snowplower', material('snow', 'snowPlowerLimit'))
add('Cookie Crumbler', material('cookie', 'cookieCrumblerLimit'))
add('Chocolate Lover', material('chocolate', 'chocolateLoverLimit'))
add('Cloud Buster', material('clouds', 'cloudBusterLimit'))
add('Pennywise', material('money', 'pennywiseLimit'))
add("Leprechaun's Secret", material('goldpot', 'leprechaunsSecretLimit'))
add('The Egg Man', material('easteregg', 'eggManLimit'))
add('Bunny Hunter', material('bunny', 'bunnyHunterLimit'))
add('Bunny Hopper', material('bunnyStatue', 'bunnyHopperLimit'))
add('Oops a Daisy', material('summerFlower', 'oopsADaisyLimit'))
add('Picnic Pooper', material('summerPicnic', 'picnicPooperLimit'))
add('Shut the Lights!', material('chineseLantern', 'shutTheLightsLimit'))
add('All Around the Globe', function() return text('globe') end)
add('Loop-de-loop-de-loop', function() return text('portal'):format(number(processor().birdThroughPortalLimit or 0)) end)
local function feathers() return (calculateAllFeathers()) end
add('Aquiline Benefactor', function() return text('eagle_text') end, true)
add('Feather Picker', counter('feather_text', feathers, 'featherPickerLimit'), true)
add('Feather Collector', counter('feather_text', feathers, 'featherCollectorLimit'), true)
add('Feather Gatherer', counter('feather_text', feathers, 'featherGathererLimit'), true)

local function listed()
    local list, with_eagle = {}, eagle()
    for _, a in ipairs(ACHIEVEMENTS) do
        if g_achievements[a.name] and (with_eagle or not a.eagle) then list[#list + 1] = a end
    end
    return list
end
local BY_NAME = {}
for _, a in ipairs(ACHIEVEMENTS) do BY_NAME[a.name] = a end
local function unlocked(a) return settingsWrapper:isAchievementAlreadyUnlocked(a.name) end

-- offline-tournament: the Pig Challenge's number of weeks.
local function tournament_weeks() return offlineTournamentWeeks and offlineTournamentWeeks() or 0 end

-- The journal's tiers; its Challenge tab (the Pig Challenge) with offline-tournament.
local function journal()
    local done, total = 0, 0
    local challenges = tournament_weeks() > 0
    for _, category in ipairs((g_new_achievements or {}).categories or {}) do
        if category.category ~= 'challenges' or challenges then
            for _, a in ipairs(category.achievements or {}) do
                local counts = (settings.achievementTable or {})[a.type]
                for i, tier in ipairs(a.tiers or {}) do
                    local target, count = tier, settingsWrapper:getTotalAchievementCount(a.type) or 0
                    if type(tier) == 'table' then
                        target = tier.targetCount
                        -- Tiers of a kind (trophy, egg) count that kind and better ones.
                        if tier.target and type(counts) == 'table' then count = counts[tier.target] or 0 end
                    end
                    total = total + 1
                    if settingsWrapper:isAchievementCompleted(a.id, i) or count >= target then done = done + 1 end
                end
            end
        end
    end
    return done, total
end

-- The hat journal's hats (g_journal_birds), each the reward of a quest. The 17
-- exclusive quests are not handed out: 5 hats are journal milestone rewards,
-- the last milestone (all of the journal's points, the Challenge tab included)
-- awards every hat, and offline-shop sells the other 12 (offlineShopHatQuests).
local function hats()
    local milestone, all = {}, false
    for _, limit in ipairs((g_new_achievements or {}).rewardLimits or {}) do
        for _, quest in ipairs(limit.reward and limit.reward.hats or {}) do
            if quest == 'all' then all = true else milestone[quest] = true end
        end
    end
    all = all and tournament_weeks() > 0
    local shop = offlineShopHatQuests or {}
    local done, total = 0, 0
    for _, book in ipairs((g_journal_birds or {}).pages or {}) do
        for _, tab in ipairs(book) do
            for _, frame in ipairs(tab.frames or {}) do
                local id = frame.questId
                local quest = id and g_quests[id]
                if quest and (not quest.exclusive or all or milestone[id] or shop[id]) then
                    total = total + 1
                    if settingsWrapper:isCompletedQuest(id) then done = done + 1 end
                end
            end
        end
    end
    return done, total
end

-- Levels that count: not the Pig Challenge (episode 1), paid ones only when they can be had.
local function counted_levels(fn)
    local tea, ham = tea_party(), ham_dunk()
    for i, e in ipairs(episodes()) do
        local paid_episode = e.name == 'Marie Hamtoinette' and not tea
        if i > 1 and not paid_episode then
            for _, p in ipairs(e.pages or {}) do
                for _, l in ipairs(p.levels or {}) do
                    if not (l.levelType == 'iap' and not ham) then fn(i, l) end
                end
            end
        end
    end
end

-- {label, done, total} for each part of the progress. Stars: 3 per level but
-- golden egg and feather levels (calculateEpisodeStars leaves out paid levels
-- not bought);
-- golden eggs: all golden egg levels (calculateOpenGoldenEggLevels counts
-- only the pages marked calculate_as_golden_egg_levels).
local function parts()
    local list = {}
    local counted, max, eggs, eggs_open = {}, 0, 0, 0
    counted_levels(function(i, l)
        counted[i] = true
        if l.levelType == 'golden_egg' then
            eggs = eggs + 1
            if settingsWrapper:isGoldenEggUnlocked(l.name) then eggs_open = eggs_open + 1 end
        elseif l.levelType ~= 'bonus_feather' then
            max = max + 3
        end
    end)
    local stars = 0
    for i in pairs(counted) do stars = stars + (calculateEpisodeStars(i) or 0) end
    list[#list + 1] = {text('stars'), stars, max}
    list[#list + 1] = {text('eggs'), eggs_open, eggs}
    if eagle() then
        local f, fmax = calculateAllFeathers()
        list[#list + 1] = {text('feathers'), f or 0, fmax or 0}
    end
    local achievements, count = listed(), 0
    for _, a in ipairs(achievements) do if unlocked(a) then count = count + 1 end end
    list[#list + 1] = {text('achievements'), count, #achievements}
    local done, total = journal()
    list[#list + 1] = {text('journal'), done, total}
    list[#list + 1] = {text('hats'), hats()}
    local weeks = tournament_weeks()
    if weeks > 0 then
        local trophies = 0
        for _ in pairs(settings.tournamentTrophies or {}) do trophies = trophies + 1 end
        list[#list + 1] = {text('trophies'), math.min(trophies, weeks), weeks}
    end
    return list
end

local function playtime() return (settingsWrapper:getPlaytime() or 0) + (playtimeCounter or 0) end
local function counters()
    local w = settingsWrapper
    local done, total, score = 0, 0, 0
    counted_levels(function(_, l)
        total = total + 1
        if isLevelCompleted(l.name) then done = done + 1 end
    end)
    for i in ipairs(episodes()) do score = score + (calculateEpisodeScore(i) or 0) end
    return {
        {text('levels'), number(done) .. ' / ' .. number(total)},
        {text('score'), number(score)},
        {text('playtime'), statistics.playtime(playtime(), language())},
        {text('birds'), number(w:getBirdsShot())},
        {text('pigs'), number(w:getPigsDestroyed())},
        {text('wood'), number(w:getWoodBlocksDestroyed())},
        {text('ice'), number(w:getIceBlocksDestroyed())},
        {text('stone'), number(w:getRockBlocksDestroyed())},
    }
end

-- The page, in screen pixels.
local PER_PAGE = 6
local serial = 0
local function unique(name)
    serial = serial + 1
    return name .. serial
end
local function label(parent, s, x, y, anchor, scale, font, max_width)
    local t = ui.Text:new({name = unique('label'), font = font or 'FONT_BASIC', text = tostring(s),
                           hanchor = anchor or 'LEFT', vanchor = 'TOP'})
    scale = scale or 1
    local w = (t:getWidth() or 0) * scale
    if max_width and w > max_width then scale = scale * max_width / w end
    t:setScale(scale)
    t.x, t.y = x, y
    t.scaledWidth = (t:getWidth() or 0) * scale
    parent:addChild(t)
    return t
end
local function rect(parent, x, y, w, h, r, g, b, a)
    local box = ui.Rect:new({name = unique('rect')})
    box.width, box.height = w, h
    box.r, box.g, box.b, box.a = r, g, b, a or 1
    box.x, box.y = x, y
    parent:addChild(box)
    return box
end
local function image(parent, sprite, x, y, scale)
    local i = ui.Image:new({name = unique('image')})
    i:setImage(sprite)
    i:setScale(scale or 1)
    i.x, i.y = x, y
    parent:addChild(i)
    return i
end
local function button(parent, name, sprite, x, y, scale)
    local b = ui.ImageButton:new({name = name})
    b:setImage(sprite)
    b:setScale(scale or 1)
    b.x, b.y = x, y
    b.returnValue = name
    parent:addChild(b)
    return b
end

GameStatisticsPage = ui.Frame:inherit()

function GameStatisticsPage:init()
    ui.Frame.init(self, {name = 'gameStatisticsPage'})
    local W, H = screenWidth, screenHeight
    local s = H / 720
    self.x, self.y, self.w, self.h = 0, 0, W, H
    pcall(processor().checkForAchievements, processor())
    rect(self, 0, 0, W, H, 0, 0, 0, 0.6)
    local box = ui.BGBox:new({name = 'box'})
    box:setSize(W * 0.92, H * 0.92)
    box.x, box.y = W * 0.04, H * 0.04
    self:addChild(box)
    label(self, text('title'), W / 2, H * 0.07, 'HCENTER', 1, 'FONT_HEADER')
    button(self, 'STATISTICS_CLOSE', 'CLOUD_DIALOG_BUTTON_CLOSE', W * 0.95, H * 0.07, 0.75 * s)

    -- left: the progress and the counters
    local left, right = W * 0.08, W * 0.47
    local list = parts()
    local total = statistics.progress(list)
    local y = H * 0.18
    local percent = label(self, string.format('%d %%', math.floor(total * 100)), right, y, 'RIGHT', 0.8)
    label(self, text('progress'), left, y, 'LEFT', 0.8, nil, right - left - percent.scaledWidth - 12 * s)
    y = y + 40 * s
    rect(self, left, y, right - left, 22 * s, 0, 0, 0, 0.35)
    rect(self, left + 3 * s, y + 3 * s, math.max((right - left - 6 * s) * total, 0), 16 * s, 1, 0.8, 0.1)
    y = y + 38 * s
    local row = 31 * s
    for _, p in ipairs(list) do
        local v = label(self, number(p[2]) .. ' / ' .. number(p[3]), right, y, 'RIGHT', 0.65)
        label(self, p[1], left, y, 'LEFT', 0.65, nil, right - left - v.scaledWidth - 10 * s)
        y = y + row
    end
    y = y + 12 * s
    for _, c in ipairs(counters()) do
        local v = label(self, c[2], right, y, 'RIGHT', 0.65)
        label(self, c[1], left, y, 'LEFT', 0.65, nil, right - left - v.scaledWidth - 10 * s)
        y = y + row
    end

    -- right: the achievements, a page at a time
    self.list = listed()
    local count = 0
    for _, a in ipairs(self.list) do if unlocked(a) then count = count + 1 end end
    self.area = {x0 = W * 0.53, x1 = W * 0.92, y = H * 0.18}
    local n = label(self, number(count) .. ' / ' .. number(#self.list), self.area.x1, self.area.y, 'RIGHT', 0.8)
    label(self, text('achievements'), self.area.x0, self.area.y, 'LEFT', 0.8, nil,
          self.area.x1 - self.area.x0 - n.scaledWidth - 12 * s)
    self.pages = math.max(1, math.ceil(#self.list / PER_PAGE))
    local by = H * 0.87
    self.back = button(self, 'STATISTICS_BACK', 'BUTTON_ARROW_LEFT', self.area.x0 + 30 * s, by, 0.6 * s)
    self.forward = button(self, 'STATISTICS_FORWARD', 'BUTTON_ARROW_RIGHT', self.area.x1 - 30 * s, by, 0.6 * s)
    self.pageText = label(self, '', (self.area.x0 + self.area.x1) / 2, by - 14 * s, 'HCENTER', 0.65)
    self:showPage(1)
end

function GameStatisticsPage:showPage(page)
    page = math.max(1, math.min(page, self.pages))
    self.page = page
    if self.rows then self:removeChild(self.rows) end
    local rows = ui.Frame:new({name = unique('rows')})
    self.rows = rows
    self:addChild(rows)
    local s = screenHeight / 720
    local area = self.area
    local y = area.y + 48 * s
    local text_x = area.x0 + 62 * s
    for i = (page - 1) * PER_PAGE + 1, math.min(page * PER_PAGE, #self.list) do
        local a = self.list[i]
        local done = unlocked(a)
        local info, value, target = a.describe()
        local icon = image(rows, g_achievements[a.name].icon, area.x0 + 26 * s, y + 26 * s, 1.25 * s)
        if not done then icon.alpha = 0.35 end  -- (setAlpha sets the children's)
        local status
        if done then
            status = text('unlocked')
        elseif value and target then
            status = number(math.min(value, target)) .. ' / ' .. number(target)
        end
        local status_w = 0
        if status then status_w = label(rows, status, area.x1, y + 4 * s, 'RIGHT', 0.55).scaledWidth + 10 * s end
        label(rows, a.name, text_x, y, 'LEFT', 0.65, 'FONT_HEADER', area.x1 - text_x - status_w)
        label(rows, info or '', text_x, y + 30 * s, 'LEFT', 0.55, nil, area.x1 - text_x)
        y = y + 66 * s
    end
    self.pageText.text = page .. ' / ' .. self.pages
    self.back:setVisible(page > 1)
    self.forward:setVisible(page < self.pages)
end

local function close() eventManager:notify({id = events.EID_POP_FRAME}) end

function GameStatisticsPage:onPointerEvent(...)
    local result = ui.Frame.onPointerEvent(self, ...)
    if result == 'STATISTICS_CLOSE' then
        close()
    elseif result == 'STATISTICS_BACK' then
        self:showPage(self.page - 1)
    elseif result == 'STATISTICS_FORWARD' then
        self:showPage(self.page + 1)
    end
    return 'BLOCK'
end

function GameStatisticsPage:onKeyEvent(event, key)
    if event == 'RELEASE' and key == 'BACK' then close() end
    return 'BLOCK'
end

-- Unlocks: the game's popup with the name, the description and the icon that
-- Game Center gave it. The game's main loop shows the queued popups only on
-- iOS; the processor's update (empty) shows them here, 3 s apart as there.
-- (achievements.lua and the processor come after this script.)
local function patch()
    local popup = AchievementPopup or (gamelua and gamelua.AchievementPopup)
    if popup and not popup.gameStatistics then
        popup.gameStatistics = true
        local layout = popup.layout
        function popup:layout(...)
            local a = BY_NAME[self.achievement_id]
            if gameCenter ~= nil or not a then return layout(self, ...) end
            gameCenter = {achievements = {[a.name] = {title = a.name, achievedText = (a.describe()),
                                                      icon = g_achievements[a.name].icon}}}
            local results = {pcall(layout, self, ...)}
            gameCenter = nil
            if not results[1] then error(results[2], 0) end
            return unpack(results, 2)
        end
    end
    local processor = achievementProcessor
    if processor and not processor.gameStatistics then
        processor.gameStatistics = true
        local update, wait = processor.update, 0
        function processor:update(dt, ...)
            local results = {update(self, dt, ...)}
            wait = math.max(wait - (dt or 0), 0)
            -- (rendering turns physics on: not while paused)
            local paused = isGamePaused and isGamePaused()
            if not iOS and not paused and wait == 0 and #(self.achievementRenderQueue or {}) > 0 then
                wait = 3
                self:renderNextAchievementPopup()
            end
            return unpack(results)
        end
    end
end
if not _G.gameStatisticsPopups then  -- (this script runs more than once)
    _G.gameStatisticsPopups = {
        eventTriggered = function(_, event)
            local a = event.first_time_unlocked and BY_NAME[event.name]
            if a and g_achievements[a.name] and achievementProcessor then
                patch()
                achievementProcessor:addToAchievementRenderQueue(a.name)
            end
        end,
    }
    eventManager:addEventListener(events.EID_ACHIEVEMENT_UNLOCKED, _G.gameStatisticsPopups)
end

-- The button, top left (the options button is bottom left).
local init = MainMenu.init
function MainMenu:init(...)
    local results = {init(self, ...)}
    patch()
    local b = ui.ImageButton:new({name = 'statisticsButton'})
    b:setImage('BUTTON_ACHIEVEMENTS')
    b.returnValue = 'SHOW_STATISTICS'
    local pointer = b.onPointerEvent
    function b:onPointerEvent(...)
        local results = {pointer(self, ...)}
        if results[1] == 'SHOW_STATISTICS' then
            eventManager:notify({id = events.EID_PUSH_FRAME, target = GameStatisticsPage:new()})
        end
        return unpack(results)
    end
    self:addChild(b)
    return unpack(results)
end
local update = MainMenu.update
function MainMenu:update(...)
    local results = {update(self, ...)}
    local b, options = self:getChild('statisticsButton'), self:getChild('optionsButton')
    if b and options then
        b.scaleX, b.scaleY = options.scaleX, options.scaleY
        b.x, b.y = options.x, screenHeight - options.y
    end
    return unpack(results)
end
)";

class GameStatistics final : public slingshot::Mod {
public:
    GameStatistics() {
        slingshot::mods::append_lua("data/scripts/gamelogic.lua", kCounters);
        slingshot::mods::append_lua("data/scripts/menus/main_menu.lua", std::string(slingshot::mods::kStatisticsLua) + kPage);
    }
};

}  // namespace

SLINGSHOT_MOD(GameStatistics, "game-statistics",
              "Statistics, progress and achievements on a page of the main menu; unlocks show in a popup");
