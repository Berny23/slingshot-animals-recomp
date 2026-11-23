// SPDX-License-Identifier: GPL-3.0-or-later
// The game shows the menu theme of the episode last played (a level's theme
// sets settingsWrapper's current main menu theme). Until then, and after a
// game update, it is Ragnahog's (getLatestMainMenuTheme). In its place the
// main menu shows the season of the device's date: the background and music
// of an episode made for that time of year (Halloween, Christmas, Easter,
// ...). Seasons with several such episodes show a random one at each start.
// --mod-option seasonal-menu.season=<name>[:<n>] shows that season (its n-th
// episode).
//
// Every episode's menu theme is in the APK
// (scripts/themes/themeNN.lua and its loadgroup), as is its menu music
// (requiredAudio.menuMusic). The start-up splash, whose only picture is
// Ragnahog's, shows the logo of the menu theme that follows it instead.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "common/game_scripts.hpp"
#include "mod.hpp"

namespace {

struct Menu {
    const char* theme;    // menu theme (blockTable.themes, loadgroups.json)
    const char* episode;  // whose menu music plays
};

struct Season {
    const char* name;
    // Inclusive month/day windows; "easter" is the 14 days before Easter
    // Sunday to Easter Monday and comes first.
    std::vector<std::pair<int, int>> windows;  // {from month * 100 + day, to month * 100 + day}
    std::vector<Menu> menus;
};

const std::vector<Season>& seasons() {
    static const std::vector<Season> s = {
        {"easter", {}, {{"theme20menu", "episodeEasterEggs"}}},
        {"lunar-new-year", {{121, 207}}, {{"theme26menu", "episodeYearOfTheDragon"}}},
        {"valentine", {{208, 214}}, {{"theme18menu", "episodeHogsAndKisses"}}},
        {"st-patrick", {{301, 317}}, {{"theme19menu", "episodeGoGreenGetLucky"}}},
        {"spring",
         {{318, 531}},
         {{"theme27menu", "episodeCherryBlossom"},
          {"theme33menu", "episodeAbraCaBacon"},
          {"theme42menu", "episodeHamDunk"},
          {"theme46menu", "episodeFairyTale"},
          {"theme47menu", "episodeTeaParty"}}},
        {"summer",
         {{601, 820}},
         {{"theme21menu", "episodeSummerPignic"},
          {"theme28menu", "episodePiglantis"},
          {"theme35menu", "episodeSouthHamerica"},
          {"theme40menu", "episodeTiki"},
          {"theme44menu", "episodeEggsFiles"},
          {"theme48menu", "episodeSummerCamp"}}},
        {"back-to-school", {{821, 910}}, {{"theme29menu", "episodeBackToSchool"}}},
        {"moon-festival", {{911, 930}}, {{"theme23menu", "episodeMoonFestival"}}},
        {"halloween",
         {{1001, 1102}},
         {{"theme16menu", "episodeTrickOrTreat"},
          {"theme24menu", "episodeHamOWeen"},
          {"theme30menu", "episodeHauntedHogs"},
          {"theme50menu", "episodeHalloweenForest"}}},
        {"autumn", {{1103, 1130}}, {{"theme49menu", "episodePiggyWood"}}},
        {"christmas",
         {{1201, 1226}},
         {{"theme17menu", "episodeSeasonSGreedings"},
          {"theme25menu", "episodeWreckTheHalls"},
          {"theme31menu", "episodeWinterWonderham"},
          {"theme34menu", "episodeArcticEggspedition"},
          {"theme45menu", "episodeSkiResort"},
          {"theme51menu", "episodeRagnahog"}}},
        {"winter",
         {{1227, 1231}, {101, 120}, {215, 229}},
         {{"theme37menu", "episodeOnFinnIce"}, {"theme51menu", "episodeRagnahog"}}},
    };
    return s;
}

// The table SEASONS and FORCED = {name, n} or nil, as Lua.
std::string seasons_lua() {
    std::string lua = "local SEASONS = {\n";
    for (const Season& s : seasons()) {
        lua += "    {name = '" + std::string(s.name) + "', windows = {";
        for (const auto& [from, to] : s.windows) lua += "{" + std::to_string(from) + ", " + std::to_string(to) + "}, ";
        lua += "}, menus = {";
        for (const Menu& m : s.menus) lua += "{'" + std::string(m.theme) + "', '" + m.episode + "'}, ";
        lua += "}},\n";
    }
    lua += "}\nlocal FORCED = nil\n";
    const std::string* forced = slingshot::mod_option("seasonal-menu", "season");
    if (!forced) return lua;
    const size_t colon = forced->find(':');
    const std::string name = forced->substr(0, colon);
    int n = 0;
    if (colon != std::string::npos) n = std::atoi(forced->c_str() + colon + 1);
    for (const Season& s : seasons()) {
        if (name != s.name) continue;
        if (colon != std::string::npos && (n < 1 || n > int(s.menus.size()))) {
            std::fprintf(stderr, "seasonal-menu: season %s has episodes 1 to %zu\n", s.name, s.menus.size());
            return lua;
        }
        return lua + "FORCED = {'" + name + "', " + std::to_string(n) + "}\n";
    }
    std::string names;
    for (const Season& s : seasons()) names += std::string(names.empty() ? "" : ", ") + s.name;
    std::fprintf(stderr, "seasonal-menu: unknown season '%s' (seasons: %s)\n", name.c_str(), names.c_str());
    return lua;
}

// The season's menu, chosen once per run: {theme, episode}.
constexpr const char* kChoose = R"(
local function day_of_year(y, m, d)
    local days = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}
    if (y % 4 == 0 and y % 100 ~= 0) or y % 400 == 0 then days[2] = 29 end
    local n = d
    for i = 1, m - 1 do n = n + days[i] end
    return n
end
local function easter(y)  -- Easter Sunday (anonymous Gregorian algorithm): month, day
    local floor = math.floor
    local a, b, c = y % 19, floor(y / 100), y % 100
    local d, e = floor(b / 4), b % 4
    local f = floor((b + 8) / 25)
    local g = floor((b - f + 1) / 3)
    local h = (19 * a + b - d - g + 15) % 30
    local i, k = floor(c / 4), c % 4
    local l = (32 + 2 * e + 2 * i - h - k) % 7
    local m = floor((a + 11 * h + 22 * l) / 451)
    local n = h + l - 7 * m + 114
    return floor(n / 31), n % 31 + 1
end
local chosen
function seasonalMenu()
    if chosen then return chosen end
    local season, year, n
    if FORCED then
        for _, s in ipairs(SEASONS) do if s.name == FORCED[1] then season = s end end
        n = FORCED[2]
    end
    local now = gamelua.getCurrentTime and gamelua.getCurrentTime()
    year = now and now.year or 2016
    if not season and now then
        local today = now.month * 100 + now.day
        local em, ed = easter(year)
        local from = day_of_year(year, em, ed)
        local doy = day_of_year(year, now.month, now.day)
        for _, s in ipairs(SEASONS) do
            if s.name == 'easter' then
                if doy >= from - 14 and doy <= from + 1 then season = s end
            else
                for _, w in ipairs(s.windows) do
                    if today >= w[1] and today <= w[2] then season = s end
                end
            end
            if season then break end
        end
    end
    if not season then return nil end
    if not n or n == 0 then
        -- From the clock, so that the game's own random numbers stay as they are.
        local t = now and ((now.hour or 0) * 3600 + (now.minutes or 0) * 60 + (now.seconds or 0)) or 0
        n = (t + math.floor((os and os.clock and os.clock() or 0) * 1000)) % #season.menus + 1
    end
    chosen = {theme = season.menus[n][1], episode = season.menus[n][2], season = season.name}
    return chosen
end
)";

// The menu theme: the season's while the one stored is the latest episode's,
// unless a level stored it. The theme a level stored (loadAndSetTheme) is
// kept in the save as seasonalMenuPlayedTheme; other callers of
// setCurrentMainMenuTheme clear it. Its theme table is read on first use
// (refreshThemeTable, blocks.lua); the menus load its graphics as for any menu
// theme.
constexpr const char* kSettings = R"(
local function seasonal_theme()
    local menu = seasonalMenu()
    if not menu then return nil end
    local themes = blockTable and blockTable.themes
    if themes and not themes[menu.theme] and refreshThemeTable then
        pcall(refreshThemeTable, (menu.theme:gsub('menu$', '')))  -- themeNN.lua has themeNN and themeNNmenu
    end
    if themes and themes[menu.theme] then return menu.theme end
end
local current = settingsWrapper.getCurrentMainMenuTheme
local latest = settingsWrapper.getLatestMainMenuTheme
local set = settingsWrapper.setCurrentMainMenuTheme
function settingsWrapper.getCurrentMainMenuTheme(...)
    local theme = current(...)
    if theme == latest(...) and not (settings and settings.seasonalMenuPlayedTheme == theme) then
        return seasonal_theme() or theme
    end
    return theme
end
function settingsWrapper.setCurrentMainMenuTheme(self, theme, ...)
    if settings then settings.seasonalMenuPlayedTheme = seasonalMenuInLevelTheme and theme or nil end
    return set(self, theme, ...)
end
)";

// The music: the main menu plays currentMainMenuSong, which the game takes
// from the current episode; with the season's theme it is the season's
// episode's.
constexpr const char* kMainMenu = R"(
local update = MainMenuRoot.update
function MainMenuRoot:update(...)
    local menu = seasonalMenu()
    if menu and settingsWrapper:getCurrentMainMenuTheme() ~= menu.theme then menu = nil end
    local episode = menu and getLocalEpisode(menu.episode)
    local music = episode and episode.requiredAudio and episode.requiredAudio.menuMusic
    local clip = music and music.clipName
    if clip and currentMainMenuSong ~= clip then
        if not isAudioLoaded(clip) then loadAudioAssetsForEpisode(menu.episode) end
        if isAudioLoaded(clip) then
            if currentMainMenuSong and res.isAudioPlaying(currentMainMenuSong) then res.stopAudio(currentMainMenuSong) end
            currentMainMenuSong = clip
        end
    end
    return update(self, ...)
end
-- Resets the save to the latest episode through loadAndSetTheme.
local deleteCloudData = MainMenuRoot.deleteCloudData
function MainMenuRoot:deleteCloudData(...)
    local results = {deleteCloudData(self, ...)}
    if settings then settings.seasonalMenuPlayedTheme = nil end
    return unpack(results)
end
)";

// A level's theme (gamelogic.lua): see kSettings.
constexpr const char* kLevel = R"(
local loadAndSetTheme_ = loadAndSetTheme
local function done(...)
    seasonalMenuInLevelTheme = nil
    return ...
end
function loadAndSetTheme(...)
    seasonalMenuInLevelTheme = true
    return done(loadAndSetTheme_(...))
end
)";

// The start-up splash (AngryBirdsSplash): Ragnahog's picture, the only one in
// the APK. For another menu theme (the season's or the episode last played)
// it shows that theme's logoImage on black; the theme's loadgroup is loaded
// for it and let go when the splash closes.
constexpr const char* kSplash = R"(
local PICTURE_THEME = 'theme51menu'  -- the picture is Ragnahog's
local init = AngryBirdsSplash.init
function AngryBirdsSplash:init(...)
    local results = {init(self, ...)}
    local name = settingsWrapper:getCurrentMainMenuTheme()
    local bg = self:getChild('bg')
    if name and name ~= PICTURE_THEME and bg then
        local theme = blockTable.themes[name]
        if theme and theme.logoImage and pcall(dynamic.load, name) then
            self.seasonalLoadgroup = name
            bg:setImage(theme.logoImage)
        end
    end
    return unpack(results)
end
local layout = AngryBirdsSplash.layout
function AngryBirdsSplash:layout(...)
    local results = {layout(self, ...)}
    local bg = self.seasonalLoadgroup and self:getChild('bg')
    if bg then
        local w, h = res.getSpriteBounds(bg.image)
        local scale = math.min(screenWidth * 0.6 / w, screenHeight * 0.45 / h)
        bg.x, bg.y = screenWidth * 0.5, screenHeight * 0.45
        bg.scaleX, bg.scaleY = scale, scale
    end
    return unpack(results)
end
local exit = AngryBirdsSplash.onExit
function AngryBirdsSplash:onExit(...)
    local results = {exit(self, ...)}
    if self.seasonalLoadgroup then
        pcall(dynamic.release, self.seasonalLoadgroup)
        self.seasonalLoadgroup = nil
    end
    return unpack(results)
end
)";

class SeasonalMenu final : public slingshot::Mod {
public:
    SeasonalMenu() {
        using slingshot::mods::append_lua;
        const std::string choose = seasons_lua() + kChoose;
        append_lua("data/scripts/settingsWrapper.lua", choose + kSettings);
        append_lua("data/scripts/menus/main_menu.lua", kMainMenu);
        append_lua("data/scripts/gamelogic.lua", kLevel);
        append_lua("data/scripts/menus/splashes.lua", kSplash);
    }
};

}  // namespace

SLINGSHOT_MOD(SeasonalMenu, "seasonal-menu",
              "Before an episode is played, the main menu shows a random episode of the current season (Halloween, Christmas, ...)");
