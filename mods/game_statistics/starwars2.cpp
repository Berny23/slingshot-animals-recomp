// SPDX-License-Identifier: GPL-3.0-or-later
// A main menu button opens a page with the progress to 100 % and the game's
// counters.
//
// The game's quests are its achievements: the achievement page (from the
// chapter selection) lists them with their rewards, and a completed one shows
// the game's banner, offline too. The page here sums them up by the page's
// tabs. Each level of a quest counts, as in the game. Not counted: signing in to
// a Rovio account (not possible offline; the game hides it too), and the Samsung
// quest where the game does not offer it (without samsung-level).
//
// The progress is the mean of stars (all chapters) and quest levels.
//
// The quests also go to Recomp Achievements (common/recomp_achievements.hpp).
#include <string>

#include "common/game_scripts.hpp"
#include "common/recomp_achievements.hpp"
#include "common/statistics.hpp"
#include "mod.hpp"

namespace {

// The menu scripts reach the standard library through _G.
constexpr const char* kLocals = R"(
local tostring, tonumber, type, pairs, ipairs, pcall, unpack = _G.tostring, _G.tonumber, _G.type, _G.pairs, _G.ipairs, _G.pcall, _G.unpack
local string, math, table = _G.string, _G.math, _G.table
local g = gamelua
)";

constexpr const char* kPage = R"(
local TEXTS = {
    en = {
        title = 'Statistics', progress = 'Game progress', stars = 'Stars', chapters = 'Chapters',
        levels = 'Levels completed', score = 'Total score', playtime = 'Playtime', birds = 'Birds launched',
        damage = 'Total damage', coins = 'Hidden coins',
    },
    fr = {
        title = 'Statistiques', progress = 'Progression du jeu', stars = 'Étoiles', chapters = 'Chapitres',
        levels = 'Niveaux terminés', score = 'Score total', playtime = 'Temps de jeu', birds = 'Oiseaux lancés',
        damage = 'Dégâts totaux', coins = 'Pièces cachées',
    },
    it = {
        title = 'Statistiche', progress = 'Progressi del gioco', stars = 'Stelle', chapters = 'Capitoli',
        levels = 'Livelli completati', score = 'Punteggio totale', playtime = 'Tempo di gioco', birds = 'Uccelli lanciati',
        damage = 'Danni totali', coins = 'Monete nascoste',
    },
    de = {
        title = 'Statistik', progress = 'Spielfortschritt', stars = 'Sterne', chapters = 'Kapitel',
        levels = 'Abgeschlossene Level', score = 'Gesamtpunktzahl', playtime = 'Spielzeit', birds = 'Abgeschossene Vögel',
        damage = 'Gesamtschaden', coins = 'Versteckte Münzen',
    },
    es = {
        title = 'Estadísticas', progress = 'Progreso del juego', stars = 'Estrellas', chapters = 'Capítulos',
        levels = 'Niveles completados', score = 'Puntuación total', playtime = 'Tiempo de juego', birds = 'Pájaros lanzados',
        damage = 'Daño total', coins = 'Monedas ocultas',
    },
    pt = {
        title = 'Estatísticas', progress = 'Progresso do jogo', stars = 'Estrelas', chapters = 'Capítulos',
        levels = 'Níveis concluídos', score = 'Pontuação total', playtime = 'Tempo de jogo', birds = 'Pássaros lançados',
        damage = 'Dano total', coins = 'Moedas escondidas',
    },
    ja = {
        title = '記録', progress = 'ゲーム達成度', stars = 'スター', chapters = 'チャプター',
        levels = 'クリアしたレベル', score = 'トータルスコア', playtime = 'プレイ時間', birds = '攻撃回数',
        damage = 'ダメージ', coins = '集めたコイン',
    },
    ru = {
        title = 'Статистика', progress = 'Прогресс игры', stars = 'Звезды', chapters = 'Главы',
        levels = 'Пройдено уровней', score = 'Общий счет', playtime = 'Время игры', birds = 'Запущено птиц',
        damage = 'Общий урон', coins = 'Скрытые монеты',
    },
}
local function language() return tostring(_G.res.getLocale()) end
local text = statistics.texts(TEXTS, language)
local function game_text(key) return _G.res.getString('TEXTS_BASIC', key) end
local function number(n) return statistics.number(n, language()) end

-- Chapters (actions.episodeTypeTable), without the "coming soon" ones.
local function chapters()
    local list = {}
    for t, name in ipairs(g.actions.episodeTypeTable) do
        for f, folder in ipairs(g.actions.myLevelFolders[name] or {}) do
            local meta = g.actions.myEpisodesMetadata[folder] or {}
            if not meta.comingSoonChapter then list[#list + 1] = {type = t, index = f, folder = folder} end
        end
    end
    return list
end
local function stars()
    local done, total = 0, 0
    for _, c in ipairs(chapters()) do
        local _, s, max = g.calculateEpisodeScoreAndStars(c.type, c.index)
        done, total = done + s, total + max
    end
    return done, total
end
local function levels()
    local done, total = 0, 0
    for _, c in ipairs(chapters()) do
        for _, level in ipairs(g.actions.myLevels[c.folder] or {}) do
            local meta = g.actions.myLevelsMetadata[level]
            if not (meta and (meta.type or meta.ignoreScore)) then
                total = total + 1
                local h = g.highscores[level]
                if h and (h.score or 0) > 0 then done = done + 1 end
            end
        end
    end
    return done, total
end

-- Each level of a quest counts once claimed or notified, as in the game.
-- Signing in to a Rovio account is left out; the Samsung quest counts where the
-- game offers it (samsung-level).
local SKIP = {idLogin = true}
local function quests(group)
    local list = {}
    local function add(q)
        if not SKIP[q.questType] and g.isQuestSupportedByDevice(q) then list[#list + 1] = q end
    end
    for _, q in pairs(g.g_questDataTable[group] or {}) do
        if group == 'chapterQuests' then
            for _, cq in ipairs(q) do add(cq) end
        else
            add(q)
        end
    end
    return list
end
local function quest_levels(group)
    local done, total = 0, 0
    for _, q in ipairs(quests(group)) do
        local max = g.getQuestMaximumLevel(q.name)
        for level = 1, max do
            total = total + 1
            local w = g.SettingsWrapper
            if w:isQuestClaimed(q.name, level) or w:isQuestCompletionNotified(q.name, level) then done = done + 1 end
        end
    end
    return done, total
end
local function get(getter, ...)
    local ok, value = pcall(g.SettingsWrapper[getter], g.SettingsWrapper, ...)
    return ok and tonumber(value) or 0
end
local function quest_value(name)
    local ok, value = pcall(g.getCurrentQuestProgress, g.getQuest(name))
    return ok and tonumber(value) or 0
end

local GROUPS = {
    {'chapterQuests', function() return text('chapters') end},
    {'characterQuests', function() return game_text('TEXT_ACHIEVEMENT_CHARACTERS') end},
    {'overallQuests', function() return game_text('TEXT_ACHIEVEMENT_OVERALL') end},
}
local function parts()
    local s, smax = stars()
    local done, total = 0, 0
    for _, group in ipairs(GROUPS) do
        local d, t = quest_levels(group[1])
        done, total = done + d, total + t
    end
    return {{text('stars'), s, smax}, {game_text('TEXT_ACHIEVEMENT_TITLE'), done, total}}
end
local function counters()
    local done, total = levels()
    local ok, score = pcall(g.calculateScoreForAllEpisodes)
    return {
        {text('levels'), number(done) .. ' / ' .. number(total)},
        {text('score'), number(ok and score or 0)},
        {text('playtime'), statistics.playtime(get('getPlaytime'), language())},
        {text('birds'), number(get('getBirdsShot'))},
        {text('damage'), number(quest_value('Total Damage'))},
        {text('coins'), number(get('getTotalHiddenCoinsCollected'))},
    }
end

-- The page, in screen pixels. Children need unique names.
local TEXT_SCALE = 1.3
local serial = 0
local function unique(name)
    serial = serial + 1
    return name .. serial
end
-- Sizes are for a height of 720 (statistics.set_factor). Measured there: 'MMMMMMMMMM'
-- in the basic font is 247.5 wide (with the font's scale), BTN_X 97.
local text_k, sprite_k = 1, 1
local function measure_sets()
    local t = ui.Text:new({name = unique('measure'), group = '', text = 'MMMMMMMMMM', font = g.getFontBasic})
    local ok, w = pcall(g.res.getSpriteBounds, 'BTN_X')
    text_k = statistics.set_factor((t:getWidth() or 0) * (t.scaleX or 1), 247.5)
    sprite_k = statistics.set_factor(ok and w, 97)
end
local function label(parent, s, x, y, anchor, font, max_width, scale)
    local t = ui.Text:new({name = unique('label'), group = '', text = tostring(s), font = font or g.getFontBasic,
                           hanchor = anchor or 'LEFT', vanchor = 'TOP'})
    -- setScale includes the font's scale; getWidth is unscaled.
    scale = (scale or 1) * TEXT_SCALE * text_k * g.screenHeight / 720
    t:setScale(scale)
    local w = t:getWidth() * t.scaleX
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
-- A box of the game's sprites (prefix .. TOP_LEFT_ .. kind .. side), as the
-- achievement page draws them, for the side the player is on.
local function box(parent, prefix, kind, x, y, w, h)
    local side = g.SettingsWrapper:isDarkSide() and 'SITH' or 'LIGHT'
    local function sprite(part) return prefix .. part .. '_' .. kind .. '_' .. side end
    -- (setSprites, run by new, would replace the components.)
    local b = ui.BGBox:new({name = unique('box')})
    b.components = {
        topLeft = sprite('TOP_LEFT'), topMiddle = sprite('TOP_MIDDLE'), topRight = sprite('TOP_RIGHT'),
        left = sprite('MIDDLE_LEFT'), center = sprite('CENTER'), right = sprite('MIDDLE_RIGHT'),
        bottomLeft = sprite('BOTTOM_LEFT'), bottomMiddle = sprite('BOTTOM_MIDDLE'), bottomRight = sprite('BOTTOM_RIGHT'),
    }
    b:setSize(w, h)
    b.x, b.y = x, y
    parent:addChild(b)
    return b
end
local function bar(parent, x, y, w, h, fraction)
    rect(parent, x, y, w, h, 0, 0, 0, 0.6)
    local inset = h * 0.15
    rect(parent, x + inset, y + inset, math.max((w - 2 * inset) * fraction, 0), h - 2 * inset, 1, 0.8, 0.1)
end
local function button(parent, value, sprite, x, y, scale)
    local b = ui.ScallableButton:new()
    b.name = unique('button')
    b:setImage(sprite)
    b.x, b.y = x, y
    scale = (scale or 1) * sprite_k
    -- A button returns to minScale after a press.
    b.scaleX, b.scaleY, b.minScale, b.maxScale = scale, scale, scale, scale * 1.1
    b.returnValue = value
    parent:addChild(b)
    b:onEntry()
    return b
end

GameStatisticsPage = g.ui.Frame:inherit()

function GameStatisticsPage:init()
    self.name = 'gameStatisticsPage'
    measure_sets()
    self.requireInput = true
    local W, H = g.screenWidth, g.screenHeight
    local s = H / 720
    rect(self, 0, 0, W, H, 0, 0, 0, 0.6)
    box(self, 'ACHIEVEMENTBOX_', 'SHEET', W / 2, H * 0.54, W * 0.9, H * 0.84)
    box(self, 'BOX_', 'SHEET', W * 0.275, H * 0.55, W * 0.43, H * 0.74)
    box(self, 'BOX_', 'SHEET', W * 0.725, H * 0.55, W * 0.43, H * 0.74)
    label(self, text('title'), W * 0.07, H * 0.04, 'LEFT', g.getFontHeadline, nil, 2.2)
    button(self, 'STATISTICS_CLOSE', 'BTN_X', W * 0.94, H * 0.13, s * 0.8)

    -- left: the progress and its parts
    local left, right = W * 0.08, W * 0.47
    local list = parts()
    local y = H * 0.17
    local total = statistics.progress(list)
    label(self, text('progress'), left, y, 'LEFT', g.getFontBasic, nil, 1.15)
    label(self, string.format('%d %%', math.floor(total * 100)), right, y, 'RIGHT', g.getFontBasic, nil, 1.15)
    y = y + 44 * s
    bar(self, left, y, right - left, 22 * s, total)
    y = y + 44 * s
    local row = 34 * s
    for _, p in ipairs(list) do
        label(self, p[1], left, y)
        label(self, number(p[2]) .. ' / ' .. number(p[3]), right, y, 'RIGHT')
        y = y + row
    end
    -- the achievements by the game's tabs
    y = y + 16 * s
    for _, group in ipairs(GROUPS) do
        local d, t = quest_levels(group[1])
        label(self, group[2](), left, y, 'LEFT', g.getFontBasic, nil, 0.85)
        label(self, number(d) .. ' / ' .. number(t), right, y, 'RIGHT', g.getFontBasic, nil, 0.85)
        y = y + 30 * s
        bar(self, left, y, right - left, 12 * s, t > 0 and d / t or 1)
        y = y + 26 * s
    end

    -- right: the counters
    local x0, x1 = W * 0.53, W * 0.92
    y = H * 0.17
    for _, c in ipairs(counters()) do
        label(self, c[1], x0, y, 'LEFT', g.getFontBasic, (x1 - x0) * 0.7)
        label(self, c[2], x1, y, 'RIGHT')
        y = y + row
    end
end

function GameStatisticsPage:onPointerEvent(...)
    local result = g.ui.Frame.onPointerEvent(self, ...)
    if result == 'STATISTICS_CLOSE' then g.eventManager:notify({id = g.events.EID_POP_FRAME}) end
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
        -- (onEntry sets the button's drag threshold.)
        b:onEntry()
    end
    statistics.match_button(b, options)
    b.x, b.y = options.x, g.screenHeight - options.y
end
local layout = MainMenu.layout
function MainMenu:layout(...)
    local results = {layout(self, ...)}
    add_button(self)
    return unpack(results)
end

-- Recomp Achievements (common/recomp_achievements.hpp): each level of a quest is
-- one. Completing a chapter's side leads to Beaten, Bird Side's last chapter is
-- its win; the characters', the paid chapters' and the Samsung quest are bonus
-- ones (offline-shop, samsung-level).
local function slug(s) return (tostring(s):lower():gsub('[^%w]+', '_'):gsub('^_+', ''):gsub('_+$', '')) end
local function localized(t, level)
    if type(t) ~= 'table' then return t end
    local v = t[language()] or t.en_EN
    if type(v) == 'table' then return v[level] or v[#v] end
    return v
end
local function sorted_keys(t)
    local keys = {}
    for k in pairs(t or {}) do keys[#keys + 1] = k end
    table.sort(keys, function(a, b)
        if type(a) == 'number' and type(b) == 'number' then return a < b end
        return tostring(a) < tostring(b)
    end)
    return keys
end
local function recomp_achievements()
    local data, list = g.g_questDataTable, {}
    if type(data) ~= 'table' then return end
    local function add(q, prefix, group, chapter)
        if q.questType == 'idLogin' then return end
        local max = g.getQuestMaximumLevel(q.name) or 1
        for level = 1, max do
            local kind
            if q.questType == 'chapterCompleted' and not chapter:find('IAP') and not chapter:find('rebels')
                and not chapter:find('bonus') then
                kind = chapter == 'chapter_5_light' and 'win' or 'progression'
            end
            local points = 10
            if max > 1 then points = ({5, 10, 10, 25, 50})[math.min(level, 5)]
            elseif q.questType == 'chapterScore' or q.questType == 'chapterStars' or kind == 'win' then points = 25
            elseif group == 'characterQuests' then points = 5 end
            list[#list + 1] = {
                key = prefix .. slug(q.name) .. (max > 1 and ('_' .. level) or ''),
                type = kind, points = points,
                bonus = group == 'characterQuests' or q.questType == 'samsungAchievement'
                    or chapter:find('IAP') ~= nil or chapter:find('rebels') ~= nil,
                unlocked = function()
                    local w = g.SettingsWrapper
                    return w:isQuestClaimed(q.name, level) or w:isQuestCompletionNotified(q.name, level)
                end,
                describe = function()
                    if max > 1 then return localized(q.title, level), localized(q.description, level) end
                    return g.getQuestTitle(q), g.getQuestDescription(q)
                end}
        end
    end
    for _, chapter in ipairs(sorted_keys(data.chapterQuests)) do
        for _, q in ipairs(data.chapterQuests[chapter]) do add(q, slug(chapter) .. '_', 'chapterQuests', chapter) end
    end
    for _, group in ipairs({'characterQuests', 'overallQuests'}) do
        for _, k in ipairs(sorted_keys(data[group])) do add(data[group][k], '', group, '') end
    end
    _G.slingshotAchievements = list
end
local recomp_layout = MainMenu.layout
function MainMenu:layout(...)
    if not _G.slingshotAchievements then pcall(recomp_achievements) end
    return recomp_layout(self, ...)
end
)";

class GameStatistics final : public slingshot::Mod {
public:
    GameStatistics() {
        slingshot::mods::append_lua("data/scripts/menus/main_menu.lua",
                                    std::string(kLocals) + slingshot::mods::kStatisticsLua + kPage);
        slingshot::mods::report_achievements();
    }
};

}  // namespace

SLINGSHOT_MOD(GameStatistics, "game-statistics", "Statistics and progress on a page of the main menu");
