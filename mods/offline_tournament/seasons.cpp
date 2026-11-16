// SPDX-License-Identifier: GPL-3.0-or-later
// The Pig Challenge without Rovio's servers.
//
// - Online, a week of the Pig Challenge had a weekday level a day (Monday to
//   Thursday) and a weekend level; the weeks came from a cloud pack. Here the
//   weeks are made of the Pig Challenge episode's levels: each "challenge" level
//   is the weekend of a week, the "daily_pig" levels take turns on the weekdays.
//   A level opens when the one before it is done (completed, or out of tries);
//   the next week starts once the weekend level is completed (kWeeks).
// - The weeks have dates from Monday, 1 January 2001. The game's clock for them
//   follows the progress instead of the time: a day per weekday level done
//   (eggs hatch at the weekend, a week's trophy is given when the next starts).
// - PlayFab's user data (tries, weekday scores) and leaderboards are kept in the
//   settings. A weekend level's leaderboard has 999 other players, with scores
//   estimated from the level's pigs, blocks and birds; trophies come from the
//   place as online (top 2 % platinum, 10 % gold, 40 % silver). There is one
//   list of players instead of a world and a friends' list (kSummary).
// - After the last week, all weeks can be switched between and played again
//   (kReplay); a better weekend score there gives a better trophy.
// - The Pig Challenge button asks for no login, and the bird journal's
//   "Set avatar" works without one (kLogin, kAvatar).
#include "common/game_scripts.hpp"
#include "mod.hpp"

namespace {

constexpr const char* kWeeks = R"(
local DAY, WEEKDAYS, WEEKEND = 86400, 4, 5
local YEAR = 2001          -- the weeks' dates: Mondays from 1 January 2001
local OPPONENTS = 999      -- other players on a weekend level's leaderboard
local SHOWN = 50           -- leaderboard rows the menu has
local PLAYER_ID = 'offlinePlayer'
local PIG_POINTS, BLOCK_POINTS, BIRD_POINTS = 5000, 500, 10000  -- for a level's score estimate
local PAR = 0.75           -- the estimate's share that gives gold
-- share of players ahead -> score / gold score
local CURVE = {{0, 1.35}, {0.02, 1.15}, {0.1, 1}, {0.4, 0.8}, {1, 0.3}}
local TROPHY_ORDER = {bronze = 1, silver = 2, gold = 3, platinum = 4}

local function leap(y) return y % 4 == 0 and (y % 100 ~= 0 or y % 400 == 0) end
local function days(y, m, d)  -- days since an epoch, for differences
    if m <= 2 then y, m = y - 1, m + 12 end
    return 365 * y + math.floor(y / 4) - math.floor(y / 100) + math.floor(y / 400)
        + math.floor((153 * (m - 3) + 2) / 5) + d
end
local function add_days(date, n)
    local y, m, d = date.year, date.month, date.day + n
    while true do
        local length = ({31, leap(y) and 29 or 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31})[m]
        if d <= length then break end
        d, m = d - length, m + 1
        if m > 12 then m, y = 1, y + 1 end
    end
    return {year = y, month = m, day = d}
end
local function date_key(date) return date.year .. '_' .. date.month .. '_' .. date.day end
local function board_key(date) return 'tournamentLevel_' .. date_key(date) .. '_' .. WEEKEND end

-- {startDate, levelNames = {4 weekday levels, the weekend level}, folder, starLimits}
local week_list, week_of = nil, {}
local function weeks()
    if week_list then return week_list end
    local episode = getAnyEpisode and getAnyEpisode('episodeTournament')
    local weekend, weekday, folder = {}, {}, {}
    for _, page in ipairs(episode and episode.pages or {}) do
        for _, level in ipairs(page.levels or {}) do
            folder[level.name] = page.folder_name
            if level.name:find('^daily_pig') then table.insert(weekday, level)
            else table.insert(weekend, 1, level) end  -- the episode lists the newest first
        end
    end
    week_list = {}
    if #weekday < WEEKDAYS then return week_list end
    local date = {year = YEAR, month = 1, day = 1}
    for w, level in ipairs(weekend) do
        local names = {}
        for k = 0, WEEKDAYS - 1 do  -- a different set of weekday levels each week
            names[k + 1] = weekday[((w - 1) * WEEKDAYS + k + math.floor((w - 1) / WEEKDAYS)) % #weekday + 1].name
        end
        names[WEEKEND] = level.name
        week_list[w] = {startDate = date, levelNames = names, folder = folder[level.name], starLimits = level.starLimits}
        week_of[date_key(date)] = w
        date = add_days(date, 7)
    end
    return week_list
end
offlineTournamentWeeks = function() return #weeks() end  -- for game-statistics

-- settings.offlineTournament = {week = the week played, done = true after the last,
--   view = the week shown since, data = {[date key] = PlayFab user data},
--   scores = {[leaderboard key] = best score}}
local function state()
    settings.offlineTournament = settings.offlineTournament or {week = 1, data = {}, scores = {}}
    return settings.offlineTournament
end
local function save() saveLuaFileWrapper('settings.lua', 'settings', true) end
local function replaying() return state().done end
local function shown()
    local s = state()
    return s.done and s.view or s.week
end

-- The user data of the week shown.
local function store(create)
    local s, week = state(), weeks()[shown()]
    if not week then return {} end
    local key = date_key(week.startDate)
    if create then s.data[key] = s.data[key] or {} end
    return s.data[key] or {}
end

-- A weekday level is done when completed or out of tries.
local function current_index(w)
    local week = weeks()[w]
    local data = week and state().data[date_key(week.startDate)] or {}
    for i = 1, WEEKDAYS do
        local score = tonumber(data['tournamentWeekdayLevelScore_' .. i] or '') or 0
        local attempts = tonumber(data['tournamentLevelAttempts_' .. i] or '')
        if score <= 0 and attempts ~= 0 then return i end
    end
    return WEEKEND
end

-- The weekend level of the week played completed: the next week, or after the last all weeks.
local function advance()
    local s, list = state(), weeks()
    local week = list[s.week]
    if s.done or not week or (s.scores[board_key(week.startDate)] or 0) <= 0 then return end
    if s.week >= #list then s.done, s.view = true, s.week else s.week = s.week + 1 end
    save()
end

-- Seconds from the start of the week shown: a day per weekday level done; a week
-- for weeks over.
local function elapsed()
    if replaying() then return 7 * DAY + 60 end
    return (current_index(shown()) - 1) * DAY + 60
end

local timestamp = native.TimeStamp
local seconds_to_date, date_after = timestamp.getSecondsToDate, timestamp.getDateAfterSecondsFromDate
timestamp.getSecondsToDate = function(y, m, d, ...)
    local week = weeks()[shown()]
    if y ~= YEAR or not week then return seconds_to_date(y, m, d, ...) end
    local start = week.startDate
    return (days(y, m, d) - days(start.year, start.month, start.day)) * DAY - elapsed()
end
timestamp.getDateAfterSecondsFromDate = function(d, m, y, seconds, ...)
    if y ~= YEAR then return date_after(d, m, y, seconds, ...) end
    local date = add_days({year = y, month = m, day = d}, math.floor(seconds / DAY))
    return date_key(date)
end

-- The weeks instead of the cloud pack's tournament data.
local resolve = SeasonsTournamentMenu.resolveTournamentData
function SeasonsTournamentMenu:resolveTournamentData(...)
    local list = weeks()
    if #list == 0 then return resolve(self, ...) end
    advance()
    local w = shown()
    local week = list[w]
    local index = replaying() and WEEKEND or current_index(w)
    settingsWrapper:resetTournamentLevelIndices()
    self.tournamentData = {startDate = week.startDate, levelNames = week.levelNames,
                           currentLevel = week.levelNames[index], currentLevelIndex = index,
                           previous = list[w - 1] or week}  -- week 1: its own leaderboard
    if state().data[date_key(week.startDate)] then
        settingsWrapper:setCurrentTournament(week.startDate)
    elseif settingsWrapper:getCurrentTournamentDate() == nil then
        self:initializeTournamentCloudData(week.startDate)
    else
        self:resetAttempts(week.startDate)
    end
end

-- A trophy belongs to the week of its leaderboard and is named after it.
local current_trophy = SeasonsTournamentMenu.getCurrentTrophy
function SeasonsTournamentMenu:getCurrentTrophy(key, ...)
    local trophy, image = current_trophy(self, key, ...)
    local w = week_of[tostring(key):match('^tournamentLevel_(%d+_%d+_%d+)_') or '']
    if trophy and trophy.name and w then
        trophy.tournamentWeekStartDate, trophy.tournamentName = weeks()[w].startDate, 'Week ' .. w
    end
    return trophy, image
end

-- A week played again only gives a better trophy than it gave.
local reserve = settingsWrapper.reserveTrophyForTournament
function settingsWrapper:reserveTrophyForTournament(trophy, ...)
    local date = trophy and trophy.tournamentWeekStartDate
    local owned = date and self:getTournamentTrophies()[date_key(date)]
    if owned and (TROPHY_ORDER[owned.name] or 0) >= (TROPHY_ORDER[trophy.name] or 0) then return end
    return reserve(self, trophy, ...)
end

-- The weekend closes the weekday levels; in a week played again they stay open.
local page_setup = TournamentMenuPage.setup
function TournamentMenuPage:setup(data, ...)
    if replaying() and self.layoutName == 'weekday' and data and data.currentLevelIndex >= WEEKEND then
        local weekdays = {}
        for key, value in pairs(data) do weekdays[key] = value end
        weekdays.currentLevelIndex, weekdays.currentLevel = WEEKDAYS, data.levelNames[WEEKDAYS]
        data = weekdays
    end
    return page_setup(self, data, ...)
end

-- The trophy meter starts from the week's unclaimed trophy; a week played again
-- starts from the trophy it gave.
local page_event = TournamentMenuPage.eventTriggered
function TournamentMenuPage:eventTriggered(...)
    local week = weeks()[shown()]
    local owned = replaying() and week and settingsWrapper:getTournamentTrophies()[date_key(week.startDate)]
    if not owned or settingsWrapper:getUnclaimedTournamentTrophy() then return page_event(self, ...) end
    local own = rawget(settingsWrapper, 'getUnclaimedTournamentTrophy')
    settingsWrapper.getUnclaimedTournamentTrophy = function() return owned end
    local results = {pcall(page_event, self, ...)}
    settingsWrapper.getUnclaimedTournamentTrophy = own
    if not results[1] then error(results[2], 0) end
    return unpack(results, 2)
end

-- No time left to show; the weekend opens after the weekday levels.
function TournamentMenuPage:getTimeToMondayString() return '' end
function TournamentMenuPage:getTimeToFridayString(date)
    local week = weeks()[shown()]
    if replaying() or not (date and week and date_key(date) == date_key(week.startDate)) then return '' end
    local left = WEEKEND - current_index(shown())
    if left <= 0 then return '' end
    return left == 1 and '1 level' or left .. ' levels'
end

-- PlayFab, for the keys of the Pig Challenge.
local playfab = native.PlayFab
local function ours(key) return type(key) == 'string' and key:find('^tournament') ~= nil end
local function list_of(keys) return type(keys) == 'table' and keys or {keys} end
local function all_ours(keys)
    for _, key in ipairs(list_of(keys)) do if not ours(key) then return false end end
    return true
end
local function free_try(key) return replaying() and key:find('^tournamentLevelAttempts_') ~= nil end

local get_user_data = playfab.getUserData
playfab.getUserData = function(keys, ...)
    if not all_ours(keys) then return get_user_data(keys, ...) end
    local data, out = store(), {}
    for _, key in ipairs(list_of(keys)) do out[key] = free_try(key) and '3' or data[key] end
    return out
end

local set_user_data = playfab.setUserData
playfab.setUserData = function(keys, values, ...)
    if not all_ours(keys) then return set_user_data(keys, values, ...) end
    local data = store(true)
    values = list_of(values)
    for i, key in ipairs(list_of(keys)) do
        if not free_try(key) then data[key] = values[i] end
    end
    save()
    return 1
end

local set_highscore = playfab.setLeaderboardHighscore
playfab.setLeaderboardHighscore = function(key, score, ...)
    if not ours(key) then return set_highscore(key, score, ...) end
    score = tonumber(score) or 0
    local old
    if key:find('^tournamentWeekdayLevelScore_') then
        local data = store(true)
        old = tonumber(data[key] or '') or 0
        if score > old then data[key] = tostring(score) end
    else
        local scores = state().scores
        old = scores[key] or 0
        if score > old then scores[key] = score end
    end
    save()
    return {isHighscore = score > old, oldScore = old}
end

-- A weekend level's gold score: its own, or estimated from what is in it.
local function par(week)
    if week.par then return week.par end
    local limits = week.starLimits or {}
    if (limits.goldScore or 0) > 1000 then week.par = limits.goldScore return week.par end
    offlineTournamentLevel = {}
    local folder = (week.folder or '/levels/Tournament'):gsub('^/', '')
    pcall(readJSONToLuaTable, folder .. '/' .. week.levelNames[WEEKEND] .. '.json', 'offlineTournamentLevel')
    local pigs, birds, blocks = 0, 0, 0
    for _, object in pairs(offlineTournamentLevel.world or {}) do
        local definition = tostring(object.definition)
        if definition:find('Bird') then birds = birds + 1
        elseif definition:find('Pig') then pigs = pigs + 1
        elseif (object.density or 0) > 0 then blocks = blocks + 1 end
    end
    offlineTournamentLevel = nil
    local estimate = pigs * PIG_POINTS + blocks * BLOCK_POINTS + math.max(birds - 1, 0) * BIRD_POINTS
    week.par = math.max(math.floor(estimate * PAR / 1000 + 0.5), 10) * 1000
    return week.par
end

-- The other players' scores, best first.
local opponent_scores = {}
local function opponents(key)
    if opponent_scores[key] then return opponent_scores[key] end
    local w = week_of[key:match('^tournamentLevel_(%d+_%d+_%d+)_') or '']
    local scores, best = {}, w and par(weeks()[w]) or 100000
    for k = 0, OPPONENTS - 1 do
        local q, factor = (k + 0.5) / OPPONENTS, CURVE[#CURVE][2]
        for i = 2, #CURVE do
            local a, b = CURVE[i - 1], CURVE[i]
            if q <= b[1] then factor = a[2] + (b[2] - a[2]) * (q - a[1]) / (b[1] - a[1]) break end
        end
        scores[k + 1] = math.floor(best * factor / 10) * 10
    end
    opponent_scores[key] = scores
    return scores
end

-- The player's place (0 is first), nil without a score.
local function position(key)
    local best = state().scores[key]
    if not best or best <= 0 then return nil, nil end
    local place = 0
    for _, score in ipairs(opponents(key)) do
        if score > best then place = place + 1 else break end
    end
    return place, best
end

local BIRDS = {'RedBird', 'SmallBlueBird', 'YellowBird', 'BlackBird', 'BasicBird2', 'BoomerangBird', 'RedBigBird', 'PinkBird', 'GlobeBird'}
local function player_entry(key)
    local place, best = position(key)
    return {displayName = 'You', score = best, position = place, playFabId = PLAYER_ID,
            AvatarBird = settings.tournamentAvatar or settingsWrapper:convertBirdHatIdToAvatarId('RedBird')}
end
local function entry(key, k)
    local place = position(key)
    if place == k then return player_entry(key) end
    local i = (place and k > place) and k - 1 or k
    local seed = 0
    for c in key:gmatch('%d') do seed = seed * 3 + tonumber(c) end
    return {displayName = 'Guest' .. (10000 + (i * 7919 + seed * 104729) % 90000), score = opponents(key)[i + 1],
            position = k, playFabId = 'offline' .. i,
            AvatarBird = settingsWrapper:convertBirdHatIdToAvatarId(BIRDS[(i + seed) % #BIRDS + 1])}
end

local cached = {}
local size_and_position = playfab.getLeaderboardSizeAndPosition
playfab.getLeaderboardSizeAndPosition = function(key, ...)
    if not ours(key) then return size_and_position(key, ...) end
    local place = position(key)
    if place then return OPPONENTS + 1, place end
    return OPPONENTS, -1
end

-- One list of all players: the top ones, and the player's own row at its end when
-- the place is further down.
local function rows(key, start, count, own_row)
    local size, place = OPPONENTS + (position(key) and 1 or 0), position(key)
    local entries = {}
    for k = start, math.min(start + count, size) - 1 do entries[#entries + 1] = entry(key, k) end
    if own_row and place and place >= start + #entries and #entries > 0 then entries[#entries] = player_entry(key) end
    return entries
end

local leaderboard_async = playfab.getLeaderboardAsync
playfab.getLeaderboardAsync = function(key, start, count, tag, ...)
    if not ours(key) then return leaderboard_async(key, start, count, tag, ...) end
    if tag == 'tournamentLb_global' then count = math.min(count, SHOWN) end
    cached[key .. '|' .. tostring(tag)] = rows(key, start, count, tag == 'tournamentLb_global')
    notifyLeaderboardAvailable(key, true)
end
local cached_leaderboard = playfab.getCachedLeaderboard
playfab.getCachedLeaderboard = function(key, tag, ...)
    if not ours(key) then return cached_leaderboard(key, tag, ...) end
    return cached[key .. '|' .. tostring(tag)]
end

-- The friends' list (the only one shown) has all players.
local friend_async = playfab.getFriendLeaderboardAsync
playfab.getFriendLeaderboardAsync = function(key, start, count, tag, ...)
    if not ours(key) then return friend_async(key, start, count, tag, ...) end
    cached['friend|' .. key .. '|' .. tostring(tag)] = rows(key, 0, SHOWN, true)
    notifyFriendLeaderboardAvailable(key, true)
end
local cached_friends = playfab.getCachedFriendLeaderboard
playfab.getCachedFriendLeaderboard = function(key, tag, ...)
    if not ours(key) then return cached_friends(key, tag, ...) end
    return cached['friend|' .. key .. '|' .. tostring(tag)]
end

-- The weekend page shows that list without the switch to the world list.
local setup_frame = TournamentLeaderboardFrame.setup
function TournamentLeaderboardFrame:setup(data, kind, ...)
    self.hideFacebookButton = true
    local switch = self:getChild('selectorBg')
    if switch then
        switch.visible = false
        kind = 'friend'
    end
    return setup_frame(self, data, kind, ...)
end

-- The player's row is told apart by the PlayFab id.
local setup_leaderboard = TournamentLeaderboardFrame.setupLeaderboard
function TournamentLeaderboardFrame:setupLeaderboard(key, ...)
    if not ours(key) then return setup_leaderboard(self, key, ...) end
    local id = playfab.getPlayerPlayFabId
    playfab.getPlayerPlayFabId = function() return PLAYER_ID end
    local results = {pcall(setup_leaderboard, self, key, ...)}
    playfab.getPlayerPlayFabId = id
    if not results[1] then error(results[2], 0) end
    return unpack(results, 2)
end
)";

// The week shown, at the top; after the last week, arrows beside it switch
// between the weeks (follows kWeeks in the same script).
constexpr const char* kReplay = R"(
local LABEL_X, ARROW_GAP, TOP = 315, 115, 42  -- in 720p units from the top left

local function refresh(self)
    local nav = self.offlineWeeks
    if not nav then return end
    local list, w, s = weeks(), shown(), screenHeight / 720
    nav.label.text = 'Week ' .. w .. ' / ' .. #list
    nav.label:setScale(0.8 * s)
    nav.label.x, nav.label.y = LABEL_X * s, TOP * s
    for step, button in pairs({[-1] = nav.back, [1] = nav.forward}) do
        local target = list[w + step]
        button:setScale(0.55 * s)
        button.x, button.y = (LABEL_X + step * ARROW_GAP) * s, TOP * s
    end
end

-- Entering the menu shows all its children: the arrows' state is kept up every frame.
local function show_arrows(self)
    local nav = self.offlineWeeks
    if not nav then return end
    for step, button in pairs({[-1] = nav.back, [1] = nav.forward}) do
        local on = (replaying() and weeks()[shown() + step] ~= nil) == true
        if button.visible ~= on then
            button.visible = on
            button:setEnabled(on)
        end
    end
end

local create = SeasonsTournamentMenu.createButtons
function SeasonsTournamentMenu:createButtons(...)
    local results = {create(self, ...)}
    local nav = {label = ui.Text:new({name = 'offlineWeekLabel', font = 'FONT_HEADER', text = '',
                                      hanchor = 'HCENTER', vanchor = 'VCENTER'})}
    self:addChild(nav.label)
    for key, side in pairs({back = {'BUTTON_ARROW_LEFT', -1}, forward = {'BUTTON_ARROW_RIGHT', 1}}) do
        local button = ui.ImageButton:new({name = 'offlineWeek_' .. key})
        button:setImage(side[1])
        button.returnValue = 'OFFLINE_WEEK_' .. key
        button.step = side[2]
        self:addChild(button)
        nav[key] = button
    end
    self.offlineWeeks = nav
    return unpack(results)
end

local layout = SeasonsTournamentMenu.layout
function SeasonsTournamentMenu:layout(...)
    refresh(self)  -- before the layout, which takes the children's places for input
    return layout(self, ...)
end

-- The pages in front take the input: the arrows are tested here (press and release on one).
local function arrow_at(self, x, y)
    local nav = self.offlineWeeks
    for _, button in ipairs(nav and {nav.back, nav.forward} or {}) do
        local half_w, half_h = button.w * (button.scaleX or 1) / 2, button.h * (button.scaleY or 1) / 2
        if button.visible and math.abs(x - button.x) <= half_w and math.abs(y - button.y) <= half_h then return button end
    end
end
local pointer = SeasonsTournamentMenu.onPointerEvent
function SeasonsTournamentMenu:onPointerEvent(event, x, y, ...)
    local button = x and y and arrow_at(self, x, y)
    if event == 'LPRESS' then self.offlineWeekPressed = button end
    if button and button == self.offlineWeekPressed then
        if event == 'LRELEASE' then
            self.offlineWeekStep, self.offlineWeekPressed = button.step, nil
            res.playAudio('menu_confirm', 1, false)
        end
        return 'BLOCK'
    end
    return pointer(self, event, x, y, ...)
end

-- The week shown changes as on entering the menu.
local function switch(self, step)
    local s, list = state(), weeks()
    local target = shown() + step
    if not s.done or not list[target] then return end
    s.view = target
    save()
    self.tournamentData = nil
    g_tournamentLevelScores = nil
    self:resolveTournamentData()
    for _, page in ipairs(self.pages or {}) do page:setup(self.tournamentData) end
    refresh(self)
end

local update = SeasonsTournamentMenu.update
function SeasonsTournamentMenu:update(...)
    local step = self.offlineWeekStep
    self.offlineWeekStep = nil
    if step then switch(self, step) end
    show_arrows(self)
    return update(self, ...)
end
)";

// The Pig Challenge button opens the challenge without a login (the episode
// selection's class goes before the snippet).
constexpr const char* kLogin = R"(
local pointer = class.onPointerEvent
function class:onPointerEvent(...)
    local wrapper = settingsWrapper
    local own, status = rawget(wrapper, 'getLoginStatus'), wrapper.getLoginStatus
    wrapper.getLoginStatus = function(...)
        local result = status(...)
        if result == 'loggedOut' then return 'guest' end
        return result
    end
    local results = {pcall(pointer, self, ...)}
    wrapper.getLoginStatus = own
    if not results[1] then error(results[2], 0) end
    return unpack(results, 2)
end
)";

// The bird journal's "Set avatar" button, for the Pig Challenge's leaderboards.
constexpr const char* kAvatar = R"(
local init = BirdJournal.init
function BirdJournal:init(...)
    local wrapper = gamelua.settingsWrapper
    local own, status = rawget(wrapper, 'getLoginStatus'), wrapper.getLoginStatus
    wrapper.getLoginStatus = function(...)
        local result = status(...)
        if result == 'loggedOut' then return 'guest' end
        return result
    end
    local results = {pcall(init, self, ...)}
    wrapper.getLoginStatus = own
    if not results[1] then error(results[2], 0) end
    return unpack(results, 2)
end
)";

// The week's results show the one list of players, in the middle.
constexpr const char* kSummary = R"(
local layout = ShowTournamentSummary.layout
function ShowTournamentSummary:layout(...)
    local results = {layout(self, ...)}
    local box = self:getChild('bg')
    local world, friends = box and box:getChild('globalStanding'), box and box:getChild('friendsStanding')
    if world and friends then
        for _, name in ipairs({'globalLogo', 'friendsLogo'}) do
            local logo = box:getChild(name)
            if logo then logo.visible = false end
        end
        world.visible = false
        self.oneList = self.oneList or {world.x, friends.x}  -- where layout put them first
        friends.x = (self.oneList[1] + self.oneList[2]) / 2
    end
    return unpack(results)
end
)";

class OfflineTournament final : public slingshot::Mod {
public:
    OfflineTournament() {
        using slingshot::mods::append_lua;
        append_lua("data/scripts/menus/tournament/seasons_tournament_menu.lua", std::string(kWeeks) + kReplay);
        const std::string login = kLogin;
        append_lua("data/scripts/menus/combinedMenu/EpisodeSelectionFrame.lua",
                   "local class = EpisodeSelectionFrame\n" + login);
        append_lua("data/scripts/menus/seasons_episode_selection.lua",
                   "local class = SeasonsEpisodeSelection\n" + login);
        append_lua("data/scripts/menus/Journal/BirdJournal.lua", kAvatar);
        append_lua("data/scripts/menus/tournament/ShowTournamentSummary.lua", kSummary);
    }
};

}  // namespace

SLINGSHOT_MOD(OfflineTournament, "offline-tournament",
              "Play the Pig Challenge without Rovio's servers: a week's levels open one after another, "
              "the leaderboards are simulated, and all weeks can be played again after the last");
