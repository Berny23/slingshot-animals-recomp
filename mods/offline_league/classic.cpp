// SPDX-License-Identifier: GPL-3.0-or-later
// The Mighty League without Rovio's servers.
//
// - The league settings are stored per account id, which is "" offline; the
//   engine's saveLuaFile writes that key as invalid Lua and the next start drops
//   them. The id becomes "offline" (kAccountId).
// - Saves that lost them have Feather League stars the tutorial doesn't know of:
//   the egg meter's wait that blocks all touches is skipped (kEggMeter), and steps
//   waiting for already completed levels move on (kCatchUp).
// - After the warm-up the game waits for the league server. Instead, the warm-up's
//   code runs one round per league from Glass to Master with levels of the league
//   episode MLEXTRA (kRounds). After Master, all leagues played can be switched
//   between (kReplay).
#include "common/game_scripts.hpp"
#include "mod.hpp"

namespace {

constexpr const char* kAccountId = R"(
local league = native.ModernLeague
if league and league.getAccountId then
    local get = league.getAccountId
    league.getAccountId = function(...)
        local id = get(...)
        if id == nil or id == '' then return 'offline' end
        return id
    end
end
)";

// After the Master round (kRounds) each league shown keeps its own egg claims.
constexpr const char* kEggClaims = R"(
local function replay_eggs(self)
    local round = self:getAccountSpecificLeagueSettings().offlineRounds
    if not (round and round.done) then return nil end
    round.eggs = round.eggs or {}
    round.eggs[round.view] = round.eggs[round.view] or {}
    return round.eggs[round.view]
end
local get_claimed, set_claimed = settingsWrapper.getLeagueModernTutorialClaimed, settingsWrapper.setLeagueModernTutorialClaimed
function settingsWrapper:getLeagueModernTutorialClaimed(id, ...)
    local eggs = replay_eggs(self)
    if eggs then return eggs[id] end
    return get_claimed(self, id, ...)
end
function settingsWrapper:setLeagueModernTutorialClaimed(id, claimed, ...)
    local eggs = replay_eggs(self)
    if not eggs then return set_claimed(self, id, claimed, ...) end
    eggs[id] = claimed
    self:saveSettings()
end
)";

constexpr const char* kEggMeter = R"(
local wait = LeagueModernEggMeter.shouldWaitForTutorialUnlock
function LeagueModernEggMeter:shouldWaitForTutorialUnlock(...)
    local league = gamelua.leagueModern
    if league and league:isInLeagueTutorial() then
        local step = gamelua.settingsWrapper:getLeagueModernTutorialStep() or 0
        if step < league.TUTORIAL_WAIT_SECOND_LEVEL_COMPLETE then return false end
    end
    return wait(self, ...)
end
)";

constexpr const char* kCatchUp = R"(
local function completed_levels(league)
    local count = 0
    for _, level in ipairs(league.tutorialLevels or {}) do
        local entry = gamelua.highscores and gamelua.highscores[level.levelName]
        if entry and entry.completed then count = count + 1 end
    end
    return count
end
local function rounds()
    return gamelua.settingsWrapper:getAccountSpecificLeagueSettings().offlineRounds
end
local function catch_up(view)
    local league = gamelua.leagueModern
    if not league or league.isClaimingEgg or view.shouldDisplayTutorial or view.isTransitioning
        or not league:isInLeagueTutorial() or (rounds() or {}).done then
        return
    end
    local waiting = {  -- step -> the completed level it waits for
        [league.TUTORIAL_WAIT_FIRST_LEVEL_COMPLETE] = 1,
        [league.TUTORIAL_WAIT_SECOND_LEVEL_COMPLETE] = 2,
        [league.TUTORIAL_WAIT_THIRD_LEVEL_COMPLETE] = 3,
        [league.TUTORIAL_WAIT_FOURTH_LEVEL_COMPLETE] = 4,
        [league.TUTORIAL_WAIT_FIFTH_LEVEL_COMPLETE] = 5,
        [league.TUTORIAL_WAIT_SIXTH_LEVEL_COMPLETE] = 6,
    }
    local step = gamelua.settingsWrapper:getLeagueModernTutorialStep() or 0
    local level = waiting[step]
    if level and completed_levels(league) >= level then
        league:setTutorialStep(step + 1)
        view.shouldDisplayTutorial = true  -- the next step's message, if it has one
    end
end
local update = LeagueModernView.update
function LeagueModernView:update(...)
    catch_up(self)
    return update(self, ...)
end

-- The Master round ends with "you completed all levels" instead of a promotion.
local message = LeagueModernView.displayTutorialMessage
function LeagueModernView:displayTutorialMessage(text, ...)
    local round = rounds()
    if text == 'LEAGUE_TUTORIAL_DAY_END' and round and round.league >= gamelua.offlineLeagueCount then
        text = 'LEAGUE_ALL_GLASS_LEVELS_PASSED_TEXT'
    end
    return message(self, text, ...)
end

-- The leaderboard of a round after the warm-up is titled with its league.
local init_leaderboard = LeagueModernView._initLeaderboardData
function LeagueModernView:_initLeaderboardData(id, ...)
    local board = init_leaderboard(self, id, ...)
    local league, round = gamelua.leagueModern, rounds()
    local shown = round and (round.done and round.view or round.league) or 0
    if board and id == LeagueModernView.BOARD_MINI and shown > 0 and league:isInLeagueTutorial() then
        board.title = 'LEAGUE_NAME_' .. league.leagueNames[shown]
    end
    return board
end
)";

constexpr const char* kRounds = R"(
local LEAGUES = #LeagueModern.leagueNames          -- Glass ... Master
local TROPHIES = {'glass', 'wood', 'stone', 'bronze', 'silver', 'gold', 'master'}
local PIG_POINTS, BLOCK_POINTS = 5000, 500        -- for a level's score estimate
local EXPLANATIONS = {                             -- warm-up messages, shown once
    [LeagueModern.TUTORIAL_STAR_BAR_EXPLANATION] = true,
    [LeagueModern.TUTORIAL_LEADERBOARD_EXPLANATION] = true,
    [LeagueModern.TUTORIAL_GOOD_JOB_MESSAGE] = true,
    [LeagueModern.TUTORIAL_EGG_REWARD_EXPLANATION] = true,
}

local warmup_levels, warmup_scores, warmup_total = {}, {}, 0
for i, level in ipairs(LeagueModern.tutorialLevels) do
    warmup_levels[i] = level
    warmup_total = warmup_total + level.scoresToPass[1]
end
for i, player in ipairs(LeagueModern.tutorialMiniLeague.players) do warmup_scores[i] = player.score end

-- {league = 0 (the warm-up) .. LEAGUES, round = n, levels = {{levelName, scoresToPass}, ...},
--  history = {[league] = levels}, eggs = {[league] = {[egg id] = true}},
--  done = true once the Master round is over, view = the league shown since}
local function state()
    local settings = gamelua.settingsWrapper:getAccountSpecificLeagueSettings()
    settings.offlineRounds = settings.offlineRounds or {league = 0, round = 0}
    return settings.offlineRounds
end

-- The league on the league screen: the round's, or after the Master round the one picked.
local function shown()
    local round = state()
    if round.done then return round.view end
    return round.league
end

-- Episode MLEXTRA without the warm-up's levels, sorted by the points of all pigs and blocks.
local pool
local function level_pool()
    if pool then return pool end
    pool = {}
    for _, page in ipairs(gamelua.g_episodes.MLEXTRA.pages) do
        for _, level in ipairs(page.levels) do
            if not level.name:find('^Learning') then
                local data = {}
                gamelua.loadLuaFileToObject('levels/MightyLeagueLevels/' .. level.name .. '.lua', data, '')
                local estimate = 0
                for name in pairs(data.world or {}) do
                    if name:find('Pig') then estimate = estimate + PIG_POINTS
                    elseif not (name:find('Bird') or name:find('Static') or name:find('Decoration')) then
                        estimate = estimate + BLOCK_POINTS
                    end
                end
                table.insert(pool, {name = level.name, estimate = estimate})
            end
        end
    end
    table.sort(pool, function(a, b) return a.estimate < b.estimate end)
    return pool
end

-- The league's part of the pool: higher leagues, bigger levels.
local function slice(league)
    local all, levels = level_pool(), {}
    for i = math.floor((league - 1) * #all / LEAGUES) + 1, math.floor(league * #all / LEAGUES) do
        table.insert(levels, all[i])
    end
    return levels
end

-- Targets grow with the league.
local function with_targets(league, level)
    local target = math.max(1, math.floor(level.estimate * (0.6 + 0.1 * league) / 1000 + 0.5)) * 1000
    return {levelName = level.name,
            scoresToPass = {target, math.floor(target * 1.2 / 1000) * 1000, math.floor(target * 1.4 / 1000) * 1000}}
end

local function pick_levels(league)
    local levels, candidates = {}, slice(league)
    for i = 1, LeagueModern.LEAGUE_LEVEL_COUNT do
        levels[i] = with_targets(league, table.remove(candidates, math.random(#candidates)))
    end
    return levels
end

-- A league's levels: the warm-up's, the current round's or a stored round's.
-- Without stored levels, the completed levels of the league's part of the pool.
local function league_levels(league)
    local round = state()
    if league == 0 then return warmup_levels end
    if league == round.league and round.levels then return round.levels end
    if round.history and round.history[league] then return round.history[league] end
    local levels = {}
    for _, level in ipairs(slice(league)) do
        local entry = gamelua.highscores[level.name]
        if entry and entry.completed and #levels < LeagueModern.LEAGUE_LEVEL_COUNT then
            table.insert(levels, with_targets(league, level))
        end
    end
    if #levels == LeagueModern.LEAGUE_LEVEL_COUNT then return levels end
end

local function stars(levels)
    local count = 0
    for _, level in ipairs(levels) do
        local entry = gamelua.highscores[level.levelName]
        count = count + (gamelua.leagueModernUtils:calculateStarsFromScores(entry and entry.score or 0, level.scoresToPass) or 0)
    end
    return count
end

-- The shown league's levels replace the warm-up's; opponents scale with its targets.
local function apply()
    local levels = league_levels(shown()) or warmup_levels
    local total = 0
    for i, level in ipairs(levels) do
        LeagueModern.tutorialLevels[i] = level
        total = total + level.scoresToPass[1]
    end
    for i, player in ipairs(LeagueModern.tutorialMiniLeague.players) do
        player.score = math.floor(warmup_scores[i] * total / warmup_total / 10) * 10
    end
end

-- A round that ended keeps its levels and the eggs claimed in it.
local function keep_round(round)
    round.history, round.eggs = round.history or {}, round.eggs or {}
    if round.league > 0 then round.history[round.league] = round.levels end
    local eggs = {}
    for _, egg in ipairs(LeagueModern.eggMeterEggs) do
        eggs[egg.id] = gamelua.settingsWrapper:getLeagueModernTutorialClaimed(egg.id) or nil
    end
    round.eggs[round.league] = eggs
end

-- After the Master round: every league played can be replayed (kReplay), and the
-- tutorial stays at a step that waits for nothing. Leagues without stored egg
-- claims get the eggs their stars reached.
local function finish(league)
    local round = state()
    for page = 0, LEAGUES do
        local levels = league_levels(page)
        if levels and not round.eggs[page] then
            local eggs, count = {}, stars(levels)
            for _, egg in ipairs(LeagueModern.eggMeterEggs) do eggs[egg.id] = count >= egg.steps or nil end
            round.eggs[page] = eggs
        end
    end
    round.done, round.view = true, round.league
    gamelua.settingsWrapper:setLeagueModernTutorialStep(league.TUTORIAL_WAIT_SIXTH_LEVEL_COMPLETE)
    gamelua.settingsWrapper:saveSettings()
end

local function start_round(league)
    local round = state()
    keep_round(round)
    if round.league == LEAGUES then return finish(league) end
    round.league = round.league + 1
    round.round = round.round + 1
    round.levels = pick_levels(round.league)
    for _, level in ipairs(round.levels) do gamelua.highscores[level.levelName] = nil end
    gamelua.saveLuaFileWrapper('highscores.lua', 'highscores', true)
    for _, egg in ipairs(LeagueModern.eggMeterEggs) do gamelua.settingsWrapper:setLeagueModernTutorialClaimed(egg.id, nil) end
    gamelua.settingsWrapper:setLeagueModernTutorialStep(league.TUTORIAL_WAIT_FIRST_LEVEL_COMPLETE)
    gamelua.settingsWrapper:saveSettings()
    local book, trophy = gamelua.stickerBook, TROPHIES[round.league]
    if trophy and book:getScore(trophy) == 0 then book:addScore(trophy) end
end

local set_state = LeagueModern.setTutorialState
function LeagueModern:setTutorialState(...)
    apply()
    return set_state(self, ...)
end

local player_data = LeagueModern.getTutorialPlayerData
function LeagueModern:getTutorialPlayerData(...)
    apply()
    local data = player_data(self, ...)
    if data then
        data.league = shown()
        -- Master has no league above it: its star bar counts stars without a target
        -- (requiredStars 0, which the game shows as such) and promotes no one.
        if data.league == LEAGUES then data.requiredStars = 0 end
        -- A league played again shows the stars its place gives, as its round's end added them.
        local board = state().done and data.miniLeague
        local reward = board and board.rewardTable and board.rewardTable[board.placement or 0]
        if reward and reward.stars then data.currentStars = (data.currentStars or 0) + reward.stars end
    end
    return data
end

local set_step = LeagueModern.setTutorialStep
function LeagueModern:setTutorialStep(step, ...)
    if state().done then return end
    if state().league > 0 then
        while EXPLANATIONS[step] do step = step + 1 end
    end
    return set_step(self, step, ...)
end

-- The leagues that can be played again, from the warm-up to Master; nil before
-- the Master round is over.
function LeagueModern:getReplayLeagues()
    local round = state()
    if not round.done then return nil end
    local leagues = {}
    for league = 0, LEAGUES do
        if league_levels(league) then table.insert(leagues, league) end
    end
    return leagues, round.view
end

function LeagueModern:showReplayLeague(league)
    state().view = league
    gamelua.settingsWrapper:saveSettings()
end

-- After a round (or a warm-up finished without this mod) the game logs in to the
-- server; the next round starts instead. The data arrives 0.25 s later, as in the
-- warm-up: given at once, the screen's transitionOut hides the buttons it just showed.
local enter = LeagueModern.enterLeagueView
function LeagueModern:enterLeagueView(done, ...)
    if gamelua.settingsWrapper:getLeagueModernTutorialStep() == self.TUTORIAL_DONE then start_round(self) end
    local main = gamelua.menuManager.menus.root
    if not self:isInLeagueTutorial() or not (main and main.handleTutorialResults) then
        return enter(self, done, ...)
    end
    if main.connectingFrame then main.connectingFrame:setVisibleAndActive(false) end
    main.tweener:startAfter(0.25):onComplete(function()
        self:setTutorialState(function() main:handleTutorialResults() end)
        done(self:getTutorialPlayerData())
    end)
end

-- The player card (kPlayerCardPage) and the emblem, which the league server kept.
local MEDALS = {{'GOLD', 1}, {'SILVER', 2}, {'BRONZE', 3}, {'TOP5', 5}, {'TOP10', 10}, {'TOP100', 100}}

-- A league's place, as its leaderboard has it: one more than the opponents ahead.
local function placement(league)
    local levels = league_levels(league)
    if not levels then return nil end
    local targets, score = 0, 0
    for _, level in ipairs(levels) do
        targets = targets + level.scoresToPass[1]
        local entry = gamelua.highscores[level.levelName]
        if entry and entry.completed then score = score + (entry.score or 0) end
    end
    local place = 1
    for _, opponent in ipairs(warmup_scores) do
        if math.floor(opponent * targets / warmup_total / 10) * 10 > score then place = place + 1 end
    end
    return place
end

-- Online, each Master week gave the medal of its place; here the Master round does.
local function medals()
    local place = state().done and placement(LEAGUES)
    for _, medal in ipairs(MEDALS) do
        if place and place <= medal[2] then return {{name = medal[1], count = 1}} end
    end
    return {}
end

local player_card = LeagueModern.getPlayerCardForId
function LeagueModern:getPlayerCardForId(params, ...)
    if not params or params.id or not params.successCallback then return player_card(self, params, ...) end
    local you = gamelua.getLocalizationString('TEXT_YOU')
    local had = self.playerState
    self.playerState = had or {id = 'offline', name = you}  -- the card tells the player's own from others' by it
    params.successCallback({player = {id = self.playerState.id, profileName = you}, medals = medals()})
    self.playerState = had
end

function LeagueModern:savePlayerEmblem(emblem, success, ...)
    state().emblem = emblem
    if self.leagueStatus then self.leagueStatus.background = emblem end
    gamelua.settingsWrapper:saveSettings()
    if success then success() end
end

local set_state_emblem = LeagueModern.setTutorialState
function LeagueModern:setTutorialState(...)
    local results = {set_state_emblem(self, ...)}
    if self.leagueStatus then self.leagueStatus.background = state().emblem end  -- the emblem selector's start
    return unpack(results)
end

local tutorial_board = LeagueModern.getTutorialLeaderboard
function LeagueModern:getTutorialLeaderboard(...)
    local board = tutorial_board(self, ...)
    local you = gamelua.getLocalizationString('TEXT_YOU')
    for _, player in ipairs(board and board.players or {}) do
        if player.name == you then player.background = state().emblem end
    end
    return board
end

-- The shop's league items require being past the tutorial, which the rounds never are.
local can_access_shop = LeagueModern.canAccessLeagueShopItems
function LeagueModern:canAccessLeagueShopItems(...)
    local round = state()
    if round.done or round.league > 0 then return true end
    return can_access_shop(self, ...)
end

gamelua.offlineLeagueCount = LEAGUES  -- for the round's end; hide-dead-services keeps the league trophies
)";

// The emblem editor's preview takes the player's name from the server's player state.
constexpr const char* kEmblemSelector = R"(
local init = LeagueModernEmblemSelector.init
function LeagueModernEmblemSelector:init(...)
    local league = gamelua.leagueModern
    local had = league.playerState
    league.playerState = had or {id = 'offline', name = gamelua.getLocalizationString('TEXT_YOU')}
    local results = {pcall(init, self, ...)}
    league.playerState = had
    if not results[1] then error(results[2], 0) end
    return unpack(results, 2)
end
)";

// The trophy book's player card page shows the card instead of "finish the
// Feather League first": the offline rounds run in the tutorial.
constexpr const char* kPlayerCardPage = R"(
local init = StickerBookPagePlayerCard.init
function StickerBookPagePlayerCard:init(...)
    local league = gamelua.leagueModern
    league.isInLeagueTutorial = function() return false end
    local results = {pcall(init, self, ...)}
    league.isInLeagueTutorial = nil  -- the class's again
    if not results[1] then error(results[2], 0) end
    return unpack(results, 2)
end
)";

// A round's end: promotion to the next league (none from Master), and the
// trophies of a week's medal: "legend" for first place, "champion" for the top 5.
constexpr const char* kRoundEnd = R"(
local init = LeagueModernRewards.init
function LeagueModernRewards:init(params, ...)
    local settings = gamelua.settingsWrapper:getAccountSpecificLeagueSettings()
    local round = settings.offlineRounds
    if params and params.miniLeague and round and round.league > 0 then
        params.league = math.min(round.league + 1, gamelua.offlineLeagueCount)
        local place, book = params.miniLeague.placement or 99, gamelua.stickerBook
        if place == 1 and book:getScore('legend') == 0 then book:addScore('legend') end
        if place <= 5 and book:getScore('champion') == 0 then book:addScore('champion') end
    end
    if params and params.miniLeague and round and round.league >= gamelua.offlineLeagueCount then
        params.miniLeague.requiredStars = 0  -- Master: no league above it, the star count has no target
    end
    return init(self, params, ...)
end
)";

// Without a star target (Master) the star bar fills against 100 stars, which
// online Master weeks added up to; here it fills against one round's stars.
constexpr const char* kStarBar = R"(
local init = LeagueModernStarBar.init
function LeagueModernStarBar:init(params, ...)
    local results = {init(self, params, ...)}
    local round = gamelua.settingsWrapper:getAccountSpecificLeagueSettings().offlineRounds
    if round and round.league > 0 and self.requiredStars <= 0 and self.starCounter then
        self.starCounter:setMax(gamelua.leagueModern.TUTORIAL_REQUIRED_STARS)
    end
    return unpack(results)
end
)";

// After the Master round the league screen switches between the leagues played
// (warm-up to Master) with the sticker book's arrows, dots and slide.
constexpr const char* kReplay = R"(
local SLIDE = 0.5  -- seconds, as the sticker book's pages
local ARROW_SCALE, DOT_SCALE = 0.8, 0.7
local GAP = 12  -- between the panel and the navigation
local HEIGHT, SHORT_HEIGHT = 700, 640  -- the league's panel; shorter, the navigation fits below it

local function replaying()
    local leagues = gamelua.leagueModern:getReplayLeagues()
    return leagues and #leagues > 1
end

-- The panel scales evenly to its height; its top edge stays in place.
local init_view = LeagueModernView.init
function LeagueModernView:init(params, ...)
    if params and params.h == HEIGHT and replaying() then
        params.h, self.shortened = SHORT_HEIGHT, true
    end
    return init_view(self, params, ...)
end
local layout_view = LeagueModernView.layout
function LeagueModernView:layout(...)
    local results = {layout_view(self, ...)}
    if self.shortened and self.leagueFrame then
        self.leagueFrame.y = self.leagueFrame.y - (HEIGHT - SHORT_HEIGHT) * 0.5 * (self.leagueFrame._pScaleY or 1)
    end
    if self.leagueFrame then self.frameY = self.leagueFrame.y end  -- where it rests, not where it slides in from
    return unpack(results)
end

local function update_navigation(main)
    local nav = main.replayNavigation
    local leagues, shown = gamelua.leagueModern:getReplayLeagues()
    if not (nav and leagues) then return end
    for i, dot in ipairs(nav.dots) do
        dot:setImage(leagues[i] == shown and 'STICKER_BOOK_PAGE_INDICATOR_ACTIVE' or 'STICKER_BOOK_PAGE_INDICATOR_INACTIVE')
    end
    nav.back:setVisibleAndActive(leagues[1] ~= shown)
    nav.forward:setVisibleAndActive(leagues[#leagues] ~= shown)
end

-- Below the league's panel, the arrows under its corners.
local function place_navigation(main)
    local nav, view = main.replayNavigation, main.view
    if not (nav and view and view.leagueFrame) then return end
    local frame, scale = view.leagueFrame, main.scaleX or 1
    local frame_scale = (frame.scaleY or 1) * (frame._pScaleY or scale)
    local arrow_x = frame.w * frame_scale * 0.5 - nav.forward.w * ARROW_SCALE * scale * 0.5
    nav.y = (view.frameY or frame.y) + frame.h * frame_scale * 0.5 + (GAP + nav.forward.h * ARROW_SCALE * 0.5) * scale
    nav.back.x, nav.forward.x = -arrow_x, arrow_x
    for i, dot in ipairs(nav.dots) do
        dot.x = (i - (#nav.dots + 1) / 2) * dot.w * DOT_SCALE * 1.2 * scale
    end
end

local function arrow(main, name, image, step)
    local button = ui.ImageButton:new({name = name})
    button:setImage(image)
    button.scaleX, button.scaleY = ARROW_SCALE, ARROW_SCALE
    button.returnValue = 'REPLAY_LEAGUE_' .. step
    local pointer = button.onPointerEvent
    function button:onPointerEvent(...)
        local results = {pointer(self, ...)}
        if results[1] == self.returnValue then main.replayStep = step end  -- switched in the next update
        return unpack(results)
    end
    return button
end

local function add_navigation(main)
    if main.replayNavigation then main:removeChild(main.replayNavigation) end
    main.replayNavigation = nil
    local leagues = gamelua.leagueModern:getReplayLeagues()
    if not leagues or #leagues < 2 then return end
    local nav = ui.Frame:new({name = 'replayNavigation'})
    nav.back, nav.forward = arrow(main, 'back', 'ARROW_BUTTON_LEFT', -1), arrow(main, 'forward', 'ARROW_BUTTON_RIGHT', 1)
    nav:addChild(nav.back)
    nav:addChild(nav.forward)
    nav.dots = {}
    for i = 1, #leagues do
        nav.dots[i] = ui.Image:new({name = 'naviDot' .. i})
        nav.dots[i]:setImage('STICKER_BOOK_PAGE_INDICATOR_INACTIVE')
        nav.dots[i].scaleX, nav.dots[i].scaleY = DOT_SCALE, DOT_SCALE
        nav:addChild(nav.dots[i])
    end
    main.replayNavigation = nav
    main:addChild(nav, 3)
    update_navigation(main)
    place_navigation(main)
end

local function switch(main, step)
    local league = gamelua.leagueModern
    local leagues, shown = league:getReplayLeagues()
    if not leagues or not main.view or main.isTransitioning or main.inputBlocked then return end
    local target
    for i, page in ipairs(leagues) do
        if page == shown then target = leagues[i + step] end
    end
    if not target then return end
    league:showReplayLeague(target)
    league:setTutorialState(function() main:handleTutorialResults() end)
    local old = main.view
    local base, width = old.x or 0, gamelua.screenWidth
    main.view = LeagueModernView:new({w = 450, h = HEIGHT, loginData = league:getTutorialPlayerData()})
    main:addChild(main.view, 2)
    main:layout()
    old.x, main.view.x = base, base + step * width
    main.isTransitioning = true
    main.tweener:start()
        :moveToX(old, base - step * width, SLIDE, gamelua.tweenEaseCubicInOut)
        :moveToX(main.view, base, SLIDE, gamelua.tweenEaseCubicInOut)
        :onComplete(function()
            old:removeEventListeners()
            main:removeChild(old)
            main.isTransitioning = false
        end)
    update_navigation(main)
end

local create = LeagueModernMain.createMainView
function LeagueModernMain:createMainView(...)
    local results = {create(self, ...)}
    add_navigation(self)
    return unpack(results)
end

local layout = LeagueModernMain.layout
function LeagueModernMain:layout(...)
    local results = {layout(self, ...)}
    place_navigation(self)
    return unpack(results)
end

local buttons = LeagueModernMain.setButtonsVisible
function LeagueModernMain:setButtonsVisible(visible, ...)
    if self.replayNavigation then self.replayNavigation:setVisibleAndActive(visible) end
    return buttons(self, visible, ...)
end

local transition_out = LeagueModernMain.transitionOut
function LeagueModernMain:transitionOut(...)
    if self.replayNavigation then self.replayNavigation:setVisibleAndActive(false) end
    return transition_out(self, ...)
end

local update = LeagueModernMain.update
function LeagueModernMain:update(...)
    local step = self.replayStep
    self.replayStep = nil
    if step then switch(self, step) end
    return update(self, ...)
end
)";

class OfflineLeague final : public slingshot::Mod {
public:
    OfflineLeague() {
        using slingshot::mods::append_lua;
        append_lua("data/scripts/settingsWrapper.lua", kAccountId);
        append_lua("data/scripts/settingsWrapper.lua", kEggClaims);
        append_lua("data/scripts/league/ui/LeagueModernEggMeter.lua", kEggMeter);
        append_lua("data/scripts/league/ui/LeagueModernView.lua", kCatchUp);
        append_lua("data/scripts/league/LeagueModern.lua", kRounds);
        append_lua("data/scripts/league/ui/LeagueModernRewards.lua", kRoundEnd);
        append_lua("data/scripts/menus/StickerBookPagePlayerCard.lua", kPlayerCardPage);
        append_lua("data/scripts/league/ui/LeagueModernEmblemSelector.lua", kEmblemSelector);
        append_lua("data/scripts/league/ui/LeagueModernStarBar.lua", kStarBar);
        append_lua("data/scripts/league/ui/LeagueModernMain.lua", kReplay);
    }
};

SLINGSHOT_MOD(OfflineLeague, "offline-league",
              "Play the Mighty League without Rovio's servers: its progress is kept, and after the warm-up "
              "come rounds in the Glass to Master leagues with the league's own levels");

}  // namespace
