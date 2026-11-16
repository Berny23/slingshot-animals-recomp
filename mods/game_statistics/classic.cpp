// SPDX-License-Identifier: GPL-3.0-or-later
// A main menu button opens a page with the progress to 100 %, the game's counters
// and its Google Play achievements; unlocks show as a banner.
//
// The game unlocks and stores achievements offline (AchievementProcessor,
// settings.achievementStatus); Google Play only showed them. Their texts are in
// data/localization/TEXTS_ACHIEVEMENTS_*.dat, which Android never loads. Episodes
// 6 to 8 use episode 1's texts with their number; the feathers and the Mighty
// Eagle get texts of their own. Not listed: "Ready For More" (free version) and
// "Episode SURF - Total Destruction" (never awarded).
//
// The pig and block counters (updateDestroyedBlocksValues) only count on iOS or
// with a gamer service; here they always count.
//
// The progress is the mean of stars, golden eggs, feathers, achievements and,
// with offline-league, the leagues.
#include <string>

#include "common/game_scripts.hpp"
#include "common/statistics.hpp"
#include "mod.hpp"

namespace {

// A dummy gamer service while the counters are updated.
constexpr const char* kCounters = R"(
if not updateDestroyedBlocksValues_offline then
    local update = updateDestroyedBlocksValues
    local nothing = function() end
    local service = setmetatable({}, {__index = function() return nothing end})
    function updateDestroyedBlocksValues(...)
        if gamerService or deviceModel == 'iphone' or deviceModel == 'ipad' then return update(...) end
        gamerService = service
        local results = {pcall(update, ...)}
        gamerService = nil
        if not results[1] then error(results[2], 0) end
        return unpack(results, 2)
    end
    updateDestroyedBlocksValues_offline = true
end
)";

// The page: texts, the numbers, the popup and the main menu's button.
constexpr const char* kPage = R"(
local TEXTS = {
    en = {
        title = 'Statistics', progress = 'Game progress', stars = 'Stars',
        eggs = 'Golden eggs', feathers = 'Mighty Eagle feathers', achievements = 'Achievements',
        leagues = 'Mighty League leagues', levels = 'Levels completed', score = 'Total score',
        playtime = 'Playtime', birds = 'Birds shot', pigs = 'Pigs popped',
        wood = 'Wood blocks destroyed', ice = 'Ice blocks destroyed', stone = 'Stone blocks destroyed',
        feather_text = 'Collect %s Mighty Eagle feathers', eagle_text = 'Use the Mighty Eagle',
        unlocked = 'Unlocked', banner = 'Achievement unlocked',
    },
    de = {
        title = 'Statistik', progress = 'Spielfortschritt', stars = 'Sterne',
        eggs = 'Goldene Eier', feathers = 'Federn des Mächtigen Adlers', achievements = 'Erfolge',
        leagues = 'Ligen der Mighty League', levels = 'Geschaffte Level', score = 'Gesamtpunktzahl',
        playtime = 'Spielzeit', birds = 'Abgeschossene Vögel', pigs = 'Zerplatzte Schweine',
        wood = 'Zerstörte Holzblöcke', ice = 'Zerstörte Eisblöcke', stone = 'Zerstörte Steinblöcke',
        feather_text = 'Sammle %s Federn des Mächtigen Adlers', eagle_text = 'Setze den Mächtigen Adler ein',
        unlocked = 'Freigeschaltet', banner = 'Erfolg freigeschaltet',
    },
    fr = {
        title = 'Statistiques', progress = 'Progression du jeu', stars = 'Étoiles',
        eggs = "Œufs d'or", feathers = "Plumes de l'Aigle Vaillant", achievements = 'Succès',
        leagues = 'Ligues de la Ligue Vaillante', levels = 'Niveaux terminés', score = 'Score total',
        playtime = 'Temps de jeu', birds = 'Oiseaux lancés', pigs = 'Cochons éclatés',
        wood = 'Blocs de bois détruits', ice = 'Blocs de glace détruits', stone = 'Blocs de pierre détruits',
        feather_text = "Collectez %s plumes de l'Aigle Vaillant", eagle_text = "Utilisez l'Aigle Vaillant",
        unlocked = 'Débloqué', banner = 'Succès débloqué',
    },
    it = {
        title = 'Statistiche', progress = 'Progressi di gioco', stars = 'Stelle',
        eggs = "Uova d'oro", feathers = 'Piume della Grande Aquila', achievements = 'Obiettivi',
        leagues = 'Campionati della Grande Lega', levels = 'Livelli completati', score = 'Punteggio totale',
        playtime = 'Tempo di gioco', birds = 'Uccelli lanciati', pigs = 'Maiali scoppiati',
        wood = 'Blocchi di legno distrutti', ice = 'Blocchi di ghiaccio distrutti', stone = 'Blocchi di pietra distrutti',
        feather_text = 'Raccogli %s piume della Grande Aquila', eagle_text = 'Usa la Grande Aquila',
        unlocked = 'Sbloccato', banner = 'Obiettivo sbloccato',
    },
    es = {
        title = 'Estadísticas', progress = 'Progreso del juego', stars = 'Estrellas',
        eggs = 'Huevos de oro', feathers = 'Plumas del Águila Poderosa', achievements = 'Logros',
        leagues = 'Ligas de la Gran Liga', levels = 'Niveles completados', score = 'Puntuación total',
        playtime = 'Tiempo de juego', birds = 'Pájaros lanzados', pigs = 'Cerdos reventados',
        wood = 'Bloques de madera destruidos', ice = 'Bloques de hielo destruidos', stone = 'Bloques de piedra destruidos',
        feather_text = 'Consigue %s plumas del Águila Poderosa', eagle_text = 'Usa el Águila Poderosa',
        unlocked = 'Desbloqueado', banner = 'Logro desbloqueado',
    },
    pt = {
        title = 'Estatísticas', progress = 'Progresso do jogo', stars = 'Estrelas',
        eggs = 'Ovos de ouro', feathers = 'Penas da Águia Enorme', achievements = 'Conquistas',
        leagues = 'Ligas da Liga Poderosa', levels = 'Níveis concluídos', score = 'Pontuação total',
        playtime = 'Tempo de jogo', birds = 'Pássaros lançados', pigs = 'Porcos estourados',
        wood = 'Blocos de madeira destruídos', ice = 'Blocos de gelo destruídos', stone = 'Blocos de pedra destruídos',
        feather_text = 'Colete %s penas da Águia Enorme', eagle_text = 'Use a Águia Enorme',
        unlocked = 'Desbloqueado', banner = 'Conquista desbloqueada',
    },
    ru = {
        title = 'Статистика', progress = 'Прогресс игры', stars = 'Звезды',
        eggs = 'Золотые яйца', feathers = 'Перья Могучего орла', achievements = 'Достижения',
        leagues = 'Лиги Могучей лиги', levels = 'Пройдено уровней', score = 'Общий счет',
        playtime = 'Время в игре', birds = 'Запущено птиц', pigs = 'Лопнуто свиней',
        wood = 'Разрушено деревянных блоков', ice = 'Разрушено ледяных блоков', stone = 'Разрушено каменных блоков',
        feather_text = 'Собери перья Могучего орла: %s', eagle_text = 'Используй Могучего орла',
        unlocked = 'Получено', banner = 'Достижение получено',
    },
    ja = {
        title = '記録', progress = 'ゲームの進行度', stars = 'スター',
        eggs = '金の卵', feathers = 'マイティイーグルの羽根', achievements = 'アチーブメント',
        leagues = 'マイティリーグのリーグ', levels = 'クリアしたレベル', score = '合計スコア',
        playtime = 'プレイ時間', birds = '飛ばした鳥', pigs = 'やっつけたブタ',
        wood = '壊した木のブロック', ice = '壊した氷のブロック', stone = '壊した石のブロック',
        feather_text = 'マイティイーグルの羽根を%s枚集めよう', eagle_text = 'マイティイーグルを使おう',
        unlocked = 'アンロック済み', banner = 'アチーブメント達成！',
    },
    zh_CN = {
        title = '统计', progress = '游戏进度', stars = '星星',
        eggs = '金蛋', feathers = '神鹰羽毛', achievements = '成就',
        leagues = 'Mighty League 联赛', levels = '已完成关卡', score = '总分',
        playtime = '游戏时间', birds = '发射的小鸟', pigs = '消灭的猪',
        wood = '摧毁的木块', ice = '摧毁的冰块', stone = '摧毁的石块',
        feather_text = '收集 %s 根神鹰羽毛', eagle_text = '使用神鹰',
        unlocked = '已解锁', banner = '成就已解锁',
    },
    zh_TW = {
        title = '數據', progress = '遊戲進度', stars = '星星',
        eggs = '金蛋', feathers = 'Mighty Eagle 羽毛', achievements = '成就',
        leagues = 'Mighty League 聯賽', levels = '已完成關卡', score = '總分',
        playtime = '遊戲時間', birds = '發射的小鳥', pigs = '消滅的豬',
        wood = '摧毀的木塊', ice = '摧毀的冰塊', stone = '摧毀的石塊',
        feather_text = '收集 %s 根 Mighty Eagle 羽毛', eagle_text = '使用 Mighty Eagle',
        unlocked = '已解鎖', banner = '成就已解鎖',
    },
}
local function language() return tostring(res.getLocale()) end
local text = statistics.texts(TEXTS, language)
local function number(n) return statistics.number(n, language()) end

-- The achievement texts: loaded on first use, their language each time.
local GROUPS = {
    W = 'TEXTS_ACHIEVEMENTS_WORLDS', B = 'TEXTS_ACHIEVEMENTS_BIRDS', M = 'TEXTS_ACHIEVEMENTS_MISC',
    S = 'TEXTS_ACHIEVEMENTS_SCORE_ADDICT', D = 'TEXTS_ACHIEVEMENTS_TOTAL_DESTRUCTION',
}
local loaded = {}
local function load_texts()
    for _, group in pairs(GROUPS) do
        if not loaded[group] then
            loaded[group] = pcall(res.createTextGroupSet, localizationPath .. '/' .. group .. '.dat')
        end
        if loaded[group] then pcall(res.loadLocale, group, res.getLocale()) end
    end
end
local function game_text(group, key)
    local ok, s = pcall(res.getString, GROUPS[group], key)
    if ok and s and s ~= key then return s end
end

local function processor() return achievementProcessor or {} end
local function feathers() return (Statistics.calculateAllFeathers()) end
local function stars() return (Statistics.calculateTotalStars()) end
local function blocks()
    local w = settingsWrapper
    return w:getWoodBlocksDestroyed() + w:getIceBlocksDestroyed() + w:getRockBlocksDestroyed()
end
local function playtime() return settingsWrapper:getPlaytime() + (playtimeCounter or 0) end

-- {name in settings, text group, text key, progress = function: (value, target)}
local ACHIEVEMENTS = {}
local function add(name, group, key, progress) ACHIEVEMENTS[#ACHIEVEMENTS + 1] = {name = name, group = group, key = key, progress = progress} end
local function towards(value, limit) return function() return value(), processor()[limit] end end
for i, name in ipairs({'Herr Helmet', 'Mr Moustache', 'Defeat of The King', 'The Imposter',
        'The Mysterious Escape', 'Hovering Helmet', 'Mounting Moustache', 'Green Baron', 'Hardhat Hidalgo',
        'Mason Moustache', 'Royal Ringleader', 'Billy The Pig', 'Clint Eastbacon', 'Wild Pork Hickok',
        'Cave Explorer', 'Cave Conquer', 'King of the Caves'}) do
    add(name, 'W', 'WORLD_' .. i)
end
for i = 1, 8 do
    if i <= 6 then add('Episode ' .. i .. ' - Score Addict', 'S', 'SCORE_WORLD_' .. i, function()
        return Statistics.calculateEpisodeScore(i), g_episodes[i].score_achievement_limit end) end
    add('Episode ' .. i .. ' - Total Destruction', 'D', 'DESCTRUCTION_WORLD_' .. i, function()
        return Statistics.calculateEpisodeStars(i) end)
end
add('Split it!', 'B', 'BLUEBIRD')
add('Speed is the Essence', 'B', 'YELLOWBIRD')
add('Boom Boom!', 'B', 'BLACKBIRD')
add('Mother of all Bombs', 'B', 'WHITEBIRD')
add('Return to Sender', 'B', 'BOOMERANGBIRD')
add('Seeing Red', 'B', 'BIGBROTHERBIRD')
add('Star Collector', 'M', 'STARCOLLECTOR', towards(stars, 'starCollectorLimit'))
add('Star Gatherer', 'M', 'STARGATHERER', towards(stars, 'starGathererLimit'))
add('Egg Hunter', 'M', 'EGGHUNTER', towards(function() return Statistics.calculateOpenGoldenEggLevels() end, 'eggHunterLimit'))
add('Egg Cracker', 'M', 'EGGCRACKER')
add('Feather Picker', nil, 50, towards(feathers, 'featherPickerLimit'))
add('Feather Collector', nil, 100, towards(feathers, 'featherCollectorLimit'))
add('Feather Gatherer', nil, 200, towards(feathers, 'featherGathererLimit'))
add('Aquiline Benefactor')
add('Bird Slinger', 'M', 'BIRDSLINGER', towards(function() return settingsWrapper:getBirdsShot() end, 'birdSlingerLimit'))
add('Pig Popper', 'M', 'PIGPOPPER', towards(function() return settingsWrapper:getPigsDestroyed() end, 'pigPopperLimit'))
add('Woodpecker', 'M', 'WOODPECKER', towards(function() return settingsWrapper:getWoodBlocksDestroyed() end, 'woodPeckerLimit'))
add('Icepicker', 'M', 'ICEPICKER', towards(function() return settingsWrapper:getIceBlocksDestroyed() end, 'icePickerLimit'))
add('Stonecutter', 'M', 'STONECUTTER', towards(function() return settingsWrapper:getRockBlocksDestroyed() end, 'stoneCutterLimit'))
add('Block Smasher', 'M', 'BLOCKSMASHER', towards(blocks, 'blockSmasherLimit'))
add('Smash Maniac', 'M', 'SMASHMANIAC', towards(blocks, 'smashManiacLimit'))
add('Hard as a Rock', 'M', 'HARDASAROCK')
add('Ultimate Bejeweler', 'M', 'ULTIMATEBEJEWELER')
add('Angry Birds Fan', 'M', 'FAN', towards(playtime, 'ABFanLimit'))
add('True Angry Birds Fan', 'M', 'TRUEFAN', towards(playtime, 'trueABFanLimit'))
add('Angry Birds Addict', 'M', 'ADDICT', towards(playtime, 'ABAddictLimit'))
add('Wilhelm Tell', 'M', 'WILHELMTELL')
add("Bull's Eye", 'M', 'BULLSEYE')
add('Backward Compatibility', 'M', 'BACKWARD')
add('Space Invader', 'M', 'SPACEINVADER')
add('Cakemonger', 'M', 'CAKEMONGER')

local function unlocked(a) return settingsWrapper:isAchievementAlreadyUnlocked(a.name) end

-- Name and description in the game's language.
local function describe(a)
    if a.group == 'B' then
        return game_text('B', 'ACHIEVEMENT_NAME_' .. a.key) or a.name, game_text('B', 'ACHIEVEMENT_UNLOCKED_' .. a.key) or ''
    elseif a.group then
        local name, info = game_text(a.group, 'ACHIEVEMENT_NAME_' .. a.key), game_text(a.group, 'ACHIEVEMENT_DESCRIPTION_' .. a.key)
        if name then return name, info or '' end
        -- episodes 6 to 8: the texts of episode 1 with their numbers
        local episode = tonumber(a.key:match('(%d+)$'))
        local first = a.key:gsub('%d+$', '1')
        name, info = game_text(a.group, 'ACHIEVEMENT_NAME_' .. first), game_text(a.group, 'ACHIEVEMENT_DESCRIPTION_' .. first)
        if name and episode then
            name = name:gsub('%f[%d]1%f[%D]', tostring(episode), 1)
            if a.group == 'S' then
                info = (info or ''):gsub('[%d%.,]+%d', number(g_episodes[episode].score_achievement_limit), 1)
            else
                info = (info or ''):gsub('%f[%d]1%f[%D]', tostring(episode), 1)
            end
            return name, info
        end
        return a.name, ''
    elseif a.key then
        return a.name, text('feather_text'):format(number(a.key))
    end
    return a.name, text('eagle_text')
end

-- {label, done, total} for each part of the progress.
local function parts()
    local list = {}
    local s, max = Statistics.calculateTotalStars()
    list[#list + 1] = {text('stars'), s, max}
    -- without an argument it counts levels, not stars
    local _, egg_levels = Statistics.calculateStarsFromGoldenEggLevels()
    list[#list + 1] = {text('eggs'), Statistics.calculateOpenGoldenEggLevels(), egg_levels or 0}
    local f, fmax = Statistics.calculateAllFeathers()
    list[#list + 1] = {text('feathers'), f, fmax}
    local count = 0
    for _, a in ipairs(ACHIEVEMENTS) do if unlocked(a) then count = count + 1 end end
    list[#list + 1] = {text('achievements'), count, #ACHIEVEMENTS}
    local leagues = gamelua.offlineLeagueCount
    if leagues then
        local round = settingsWrapper:getAccountSpecificLeagueSettings().offlineRounds or {league = 0}
        list[#list + 1] = {text('leagues'), round.done and leagues or math.max((round.league or 0) - 1, 0), leagues}
    end
    return list
end

local function counters()
    local w = settingsWrapper
    local done, total, score = 0, 0, 0
    for _, id in ipairs(g_episodeIds) do
        done = done + (Statistics.calculateEpisodeLevelsCompleted(id) or 0)
        total = total + (Statistics.getLevelCountForEpisode(id) or 0)
        score = score + (Statistics.calculateEpisodeScore(id) or 0)
    end
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

local W, H = 1160, 660
local PER_PAGE = 6
local ROW = 31  -- the left column's rows

-- Russian, Japanese and Chinese have fonts of their own (reloadLocaleFonts
-- picks the one for FONT_BASIC); the other fonts lack their characters.
-- FONT_HEADER has no accented letters but German ones.
local function localized(font)
    local reload = reloadLocaleFonts or (gamelua and gamelua.reloadLocaleFonts)
    local t = {}
    if reload then reload(t) end
    if t.font and t.font ~= 'FONT_BASIC' then return t.font end
    if font == 'FONT_HEADER' and language():sub(1, 2) ~= 'en' and language():sub(1, 2) ~= 'de' then
        return 'FONT_BASIC'
    end
    return font or 'FONT_BASIC'
end
local function label(parent, s, x, y, anchor, scale, font, color)
    local t = ui.Text:new({font = localized(font), hanchor = anchor or 'LEFT'})
    t:setScale(scale or 0.56)
    t:setText(s)
    if color then t:setColorParams(color) end
    t.refX, t.refY = x, y
    parent:addChild(t)
    return t
end

-- (A Rect is drawn at its x, y from the top left; layout places it, see below.)
local function rect(parent, x, y, w, h, r, g, b, a)
    local box = ui.Rect:new()
    box.width, box.height = w, h
    box.r, box.g, box.b, box.a = r, g, b, a or 1
    box.boxX, box.boxY = x, y
    parent:addChild(box)
    parent.boxes = parent.boxes or {}
    parent.boxes[#parent.boxes + 1] = box
    return box
end

local function on_release(button, fn)
    local pointer = button.onPointerEvent
    function button:onPointerEvent(...)
        local results = {pointer(self, ...)}
        if results[1] == self.returnValue then fn() end
        return unpack(results)
    end
    return button
end

GameStatisticsPopup = _G.lua.Class.create(ui.ModernPopupBase)

function GameStatisticsPopup:init(params)
    ui.ModernPopupBase.init(self, params)
    self.w, self.h = W, H
    self:addBackground(ui.ModernPopupBase.TYPE_LEAGUE)
    self:addCloseButton()
    load_texts()
    if achievementProcessor and achievementProcessor.checkForAchievements then
        pcall(achievementProcessor.checkForAchievements, achievementProcessor)
    end
    label(self, text('title'), 0, -H * 0.5 + 50, 'HCENTER', 1, 'FONT_HEADER')

    -- left: the progress and the counters
    local left, right = -W * 0.5 + 60, -30
    local list = parts()
    local total = statistics.progress(list)
    local y = -H * 0.5 + 115
    label(self, text('progress'), left, y, 'LEFT', 0.65, 'FONT_HEADER')
    label(self, string.format('%d %%', math.floor(total * 100)), right, y, 'RIGHT', 0.65, 'FONT_HEADER')
    y = y + 30
    rect(self, left, y, right - left, 26, 0.1, 0.25, 0.45, 0.5)
    rect(self, left + 4, y + 4, math.max((right - left - 8) * total, 0), 18, 1, 0.78, 0.1)
    y = y + 64
    for _, p in ipairs(list) do
        label(self, p[1], left, y)
        label(self, number(p[2]) .. ' / ' .. number(p[3]), right, y, 'RIGHT')
        y = y + ROW
    end
    y = y + 16
    for _, c in ipairs(counters()) do
        label(self, c[1], left, y)
        label(self, c[2], right, y, 'RIGHT')
        y = y + ROW
    end

    -- right: the achievements, a page at a time
    local x0, x1 = 30, W * 0.5 - 60
    local count = 0
    for _, a in ipairs(ACHIEVEMENTS) do if unlocked(a) then count = count + 1 end end
    label(self, text('achievements'), x0, -H * 0.5 + 115, 'LEFT', 0.65, 'FONT_HEADER')
    label(self, number(count) .. ' / ' .. number(#ACHIEVEMENTS), x1, -H * 0.5 + 115, 'RIGHT', 0.65, 'FONT_HEADER')
    self.achievementArea = {x0 = x0, x1 = x1, y = -H * 0.5 + 165}
    self.pages = math.ceil(#ACHIEVEMENTS / PER_PAGE)
    self.page = 1
    local back = ui.ImageButton:new({name = 'statisticsBack'})
    back:setImage('ARROW_BUTTON_LEFT')
    back:setScale(0.6)
    back.returnValue = 'STATISTICS_BACK'
    back.refX, back.refY = x0 + 40, H * 0.5 - 70
    local forward = ui.ImageButton:new({name = 'statisticsForward'})
    forward:setImage('ARROW_BUTTON_RIGHT')
    forward:setScale(0.6)
    forward.returnValue = 'STATISTICS_FORWARD'
    forward.refX, forward.refY = x1 - 40, H * 0.5 - 70
    on_release(back, function() self:showPage(self.page - 1) end)
    on_release(forward, function() self:showPage(self.page + 1) end)
    self:addChild(back)
    self:addChild(forward)
    self.back, self.forward = back, forward
    self.pageText = label(self, '', (x0 + x1) * 0.5, H * 0.5 - 70, 'HCENTER', 0.55)
    self:showPage(1)
end

function GameStatisticsPopup:showPage(page)
    page = math.max(1, math.min(page, self.pages))
    self.page = page
    if self.achievementRows then self:removeChild(self.achievementRows) end
    local rows = ui.Frame:new({name = 'achievementRows'})
    self.achievementRows = rows
    self:addChild(rows)
    local area = self.achievementArea
    local y = area.y
    for i = (page - 1) * PER_PAGE + 1, math.min(page * PER_PAGE, #ACHIEVEMENTS) do
        local a = ACHIEVEMENTS[i]
        local done = unlocked(a)
        local name, info = describe(a)
        local dim = not done and {r = 170, g = 170, b = 170} or nil
        local star = ui.Image:new()
        star:setImage(done and 'BEST_STAR_YELLOW' or 'BEST_STAR_GREY')
        star:setScale(2.4)
        star.refX, star.refY = area.x0 + 18, y + 20
        rows:addChild(star)
        label(rows, name, area.x0 + 50, y, 'LEFT', 0.55, 'FONT_HEADER', dim)
        label(rows, info, area.x0 + 50, y + 32, 'LEFT', 0.48, 'FONT_BASIC', dim)
        local status
        if done then
            status = text('unlocked')
        elseif a.progress then
            local value, target = a.progress()
            if value and target then status = number(math.min(value, target)) .. ' / ' .. number(target) end
            if value and not target and a.group == 'D' then
                local _, max = Statistics.calculateEpisodeStars(tonumber(a.key:match('(%d+)$')))
                status = number(value) .. ' / ' .. number(max)
            end
        end
        if status then label(rows, status, area.x1, y + 4, 'RIGHT', 0.48, 'FONT_BASIC', dim) end
        y = y + 70
    end
    self.pageText:setText(page .. ' / ' .. self.pages)
    self.back:setVisible(page > 1)
    self.forward:setVisible(page < self.pages)
    if self.layout then self:layout() end
end

function GameStatisticsPopup:layout(...)
    local results = {ui.ModernPopupBase.layout(self, ...)}
    local scale = (self._pScaleX or 1) * (self.scaleX or 1)
    for _, box in ipairs(self.boxes or {}) do
        box.x, box.y = box.boxX * scale, box.boxY * scale
    end
    return unpack(results)
end

function GameStatisticsPopup:onPointerEvent(...)
    ui.ModernPopupBase.onPointerEvent(self, ...)
    return 'BLOCK'
end

-- Unlock banner, as the game's "new sticker" banner: queued, 4 s each, non-blocking.
local BY_NAME = {}
for _, a in ipairs(ACHIEVEMENTS) do BY_NAME[a.name] = a end
local function show_banner(a)
    load_texts()
    local name = describe(a)
    notificationsFrame:displayNotificationBanner(addBox(layoutHBox({
        setLayoutSize(makeImage('BUTTON_ACHIEVEMENTS', 0.8), 110, 100),
        hspacer(20),
        layoutVBox({makeText(text('banner'), 0.7, 520), makeText(name, 1, 520)}),
    })))
end
if not _G.gameStatisticsBanners then  -- (this script runs more than once)
    _G.gameStatisticsBanners = {
        eventTriggered = function(_, event)
            local a = event.first_time_unlocked and BY_NAME[event.name]
            if a then pcall(show_banner, a) end
        end,
    }
    gamelua.eventManager:addEventListener(gamelua.events.EID_ACHIEVEMENT_UNLOCKED, _G.gameStatisticsBanners)
end

-- The button, in place of the "more games" button (top left).
local init = MainMenu.init
function MainMenu:init(...)
    local results = {init(self, ...)}
    local button = ui.ImageButton:new({name = 'statisticsButton'})
    button:setImage('BUTTON_ACHIEVEMENTS')
    button.returnValue = 'SHOW_STATISTICS'
    button.clickSound = 'menu_confirm'
    on_release(button, function() league.displayModernPopup(GameStatisticsPopup:new()) end)
    self:addChild(button)
    return unpack(results)
end

-- (In update, as hide-dead-services hides that button there.)
local update = MainMenu.update
function MainMenu:update(...)
    local results = {update(self, ...)}
    local button, other = self:getChild('statisticsButton'), self:getChild('portfolioButton')
    if button and other then
        button.scaleX, button.scaleY = other.scaleX, other.scaleY
        button.x, button.y = other.x, other.y
        if other.visible ~= false then button.x = other.x + other.w * other.scaleX * 1.1 end
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
              "Statistics, progress and achievements on a page of the main menu; counts destroyed blocks and pigs");
