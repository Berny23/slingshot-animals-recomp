// SPDX-License-Identifier: GPL-3.0-or-later
// A main menu button opens a page with the progress to 100 %, the game's counters
// and its achievements; unlocks show as the game's achievement toast.
//
// The game checks its 73 achievements (checkForAchievements and the like) only
// with Game Center, and only queues the unlocks for it. Here the checks run on
// every device and the unlocks are stored in settings.gameStatisticsUnlocked. The
// game has the names and icons (gamecenter/ipad/achievements.lua) but no
// descriptions; these follow the game's conditions. The Mighty Eagle's four
// achievements are listed once the eagle can be had (offline-shop, or bought
// before).
//
// The lamp, marmoset, bird cage, crate, flower and block counters only count on
// iPhone and iPad; here they always count.
//
// The progress is the mean of stars (every level, bonus and reward level),
// hidden items, Mighty Eagle feathers (with the eagle) and achievements.
#include <string>

#include "common/game_scripts.hpp"
#include "mod.hpp"

namespace {

// Achievements, unlocks, counters and texts, in the game's globals.
constexpr const char* kGameLogic = R"(
if not gameStatisticsAchievements then

-- Texts of the page and the unlock notification.
gameStatisticsTexts = {
    en = {
        title = 'Statistics', progress = 'Game progress', stars = 'Stars', items = 'Hidden items',
        feathers = 'Mighty Eagle feathers', achievements = 'Achievements', levels = 'Levels completed',
        score = 'Total score', playtime = 'Playtime', marmosets = 'Marmosets popped',
        cages = 'Bird cages broken', crates = 'Crates broken', blocks = 'Blocks destroyed',
        unlocked = 'Unlocked', banner = 'Achievement unlocked',
        complete = 'Complete %s', three_stars = 'Get three stars on every level of %s',
        episode_score = 'Score %s points in %s', fruit = 'Find all %s hidden fruits in %s',
        fruit_salad = 'Find hidden fruits in %s episodes', star_count = 'Get %s stars',
        feather_count = 'Collect %s Mighty Eagle feathers', bonus = 'Get three stars on %s bonus levels',
        blue = 'Meet the blue birds', yellow = 'Meet the yellow bird', black = 'Meet the black bird',
        white = 'Meet the white bird', rio = 'Meet Blu and Jewel', blu = 'Meet Blu on his own',
        big = 'Meet the big red bird', boomerang = 'Meet the boomerang bird', globe = 'Meet the globe bird',
        eagle = 'Use the Mighty Eagle', crate = 'Break %s crates', flower = 'Break %s flowers',
        block = 'Destroy %s blocks', lamp = 'Break %s lamps', marmoset = 'Pop %s marmosets',
        cage = 'Break %s bird cages', credits = 'Watch the credits', total = 'Reach a total score of %s',
        friendly = 'Hit a bird with another bird', sniper = 'Hit a marmoset from far away',
        backwards = 'Launch a bird backwards %s times', first_bird = 'Complete a level with the first bird',
        gifted = 'Launch %s birds in a row that do no damage', minutes = 'Play for %s minutes',
        hours = 'Play for %s hours',
    },
    de = {
        title = 'Statistik', progress = 'Spielfortschritt', stars = 'Sterne', items = 'Versteckte Gegenstände',
        feathers = 'Federn des Mächtigen Adlers', achievements = 'Erfolge', levels = 'Geschaffte Level',
        score = 'Gesamtpunktzahl', playtime = 'Spielzeit', marmosets = 'Zerplatzte Krallenaffen',
        cages = 'Zerbrochene Vogelkäfige', crates = 'Zerbrochene Kisten', blocks = 'Zerstörte Blöcke',
        unlocked = 'Freigeschaltet', banner = 'Erfolg freigeschaltet',
        complete = 'Schließe %s ab', three_stars = 'Hol drei Sterne in jedem Level von %s',
        episode_score = 'Erziele %s Punkte in %s', fruit = 'Finde alle %s versteckten Früchte in %s',
        fruit_salad = 'Finde versteckte Früchte in %s Episoden', star_count = 'Sammle %s Sterne',
        feather_count = 'Sammle %s Federn des Mächtigen Adlers', bonus = 'Hol drei Sterne in %s Bonuslevels',
        blue = 'Lerne die blauen Vögel kennen', yellow = 'Lerne den gelben Vogel kennen',
        black = 'Lerne den schwarzen Vogel kennen', white = 'Lerne den weißen Vogel kennen',
        rio = 'Lerne Blu und Jewel kennen', blu = 'Lerne Blu allein kennen',
        big = 'Lerne den großen roten Vogel kennen', boomerang = 'Lerne den Bumerangvogel kennen',
        globe = 'Lerne den Globusvogel kennen', eagle = 'Setze den Mächtigen Adler ein',
        crate = 'Zerbrich %s Kisten', flower = 'Zerbrich %s Blumen', block = 'Zerstöre %s Blöcke',
        lamp = 'Zerbrich %s Lampen', marmoset = 'Lass %s Krallenaffen platzen', cage = 'Zerbrich %s Vogelkäfige',
        credits = 'Sieh dir den Abspann an', total = 'Erreiche eine Gesamtpunktzahl von %s',
        friendly = 'Triff einen Vogel mit einem anderen Vogel', sniper = 'Triff einen Krallenaffen aus großer Entfernung',
        backwards = 'Schieß %s-mal einen Vogel nach hinten', first_bird = 'Schaffe ein Level mit dem ersten Vogel',
        gifted = 'Schieß %s Vögel nacheinander ab, die keinen Schaden anrichten',
        minutes = 'Spiele %s Minuten', hours = 'Spiele %s Stunden',
    },
    fr = {
        title = 'Statistiques', progress = 'Progression', stars = 'Étoiles', items = 'Objets cachés',
        feathers = 'Plumes du Puissant Aigle', achievements = 'Succès', levels = 'Niveaux terminés',
        score = 'Score total', playtime = 'Temps de jeu', marmosets = 'Ouistitis éclatés',
        cages = 'Cages à oiseaux cassées', crates = 'Caisses cassées', blocks = 'Blocs détruits',
        unlocked = 'Débloqué', banner = 'Succès débloqué',
        complete = 'Terminez %s', three_stars = 'Obtenez trois étoiles dans chaque niveau de %s',
        episode_score = 'Marquez %s points dans %s', fruit = 'Trouvez les %s fruits cachés de %s',
        fruit_salad = 'Trouvez des fruits cachés dans %s épisodes', star_count = 'Obtenez %s étoiles',
        feather_count = 'Ramassez %s plumes du Puissant Aigle', bonus = 'Obtenez trois étoiles dans %s niveaux bonus',
        blue = 'Rencontrez les oiseaux bleus', yellow = "Rencontrez l'oiseau jaune", black = "Rencontrez l'oiseau noir",
        white = "Rencontrez l'oiseau blanc", rio = 'Rencontrez Blu et Perla', blu = 'Rencontrez Blu tout seul',
        big = 'Rencontrez le gros oiseau rouge', boomerang = "Rencontrez l'oiseau boomerang",
        globe = "Rencontrez l'oiseau globe", eagle = 'Utilisez le Puissant Aigle', crate = 'Cassez %s caisses',
        flower = 'Cassez %s fleurs', block = 'Détruisez %s blocs', lamp = 'Cassez %s lampes',
        marmoset = 'Faites éclater %s ouistitis', cage = 'Cassez %s cages à oiseaux',
        credits = 'Regardez le générique', total = 'Atteignez un score total de %s',
        friendly = 'Touchez un oiseau avec un autre oiseau', sniper = 'Touchez un ouistiti de très loin',
        backwards = "Lancez un oiseau vers l'arrière %s fois", first_bird = 'Terminez un niveau avec le premier oiseau',
        gifted = "Lancez %s oiseaux d'affilée qui ne font aucun dégât", minutes = 'Jouez pendant %s minutes',
        hours = 'Jouez pendant %s heures',
    },
    it = {
        title = 'Statistiche', progress = 'Progressi', stars = 'Stelle', items = 'Oggetti nascosti',
        feathers = 'Piume della Potente Aquila', achievements = 'Obiettivi', levels = 'Livelli completati',
        score = 'Punteggio totale', playtime = 'Tempo di gioco', marmosets = 'Uistitì scoppiati',
        cages = 'Gabbie rotte', crates = 'Casse rotte', blocks = 'Blocchi distrutti',
        unlocked = 'Sbloccato', banner = 'Obiettivo sbloccato',
        complete = 'Completa %s', three_stars = 'Ottieni tre stelle in ogni livello di %s',
        episode_score = 'Fai %s punti in %s', fruit = 'Trova tutti i %s frutti nascosti in %s',
        fruit_salad = 'Trova frutti nascosti in %s episodi', star_count = 'Ottieni %s stelle',
        feather_count = 'Raccogli %s piume della Potente Aquila', bonus = 'Ottieni tre stelle in %s livelli bonus',
        blue = 'Incontra gli uccelli blu', yellow = "Incontra l'uccello giallo", black = "Incontra l'uccello nero",
        white = "Incontra l'uccello bianco", rio = 'Incontra Blu e Gioiel', blu = 'Incontra Blu da solo',
        big = "Incontra il grande uccello rosso", boomerang = "Incontra l'uccello boomerang",
        globe = "Incontra l'uccello globo", eagle = 'Usa la Potente Aquila', crate = 'Rompi %s casse',
        flower = 'Rompi %s fiori', block = 'Distruggi %s blocchi', lamp = 'Rompi %s lampade',
        marmoset = 'Fai scoppiare %s uistitì', cage = 'Rompi %s gabbie', credits = 'Guarda i titoli di coda',
        total = 'Raggiungi un punteggio totale di %s', friendly = 'Colpisci un uccello con un altro uccello',
        sniper = 'Colpisci un uistitì da molto lontano', backwards = "Lancia un uccello all'indietro %s volte",
        first_bird = 'Completa un livello con il primo uccello',
        gifted = 'Lancia %s uccelli di fila che non fanno danni', minutes = 'Gioca per %s minuti',
        hours = 'Gioca per %s ore',
    },
    es = {
        title = 'Estadísticas', progress = 'Progreso', stars = 'Estrellas', items = 'Objetos ocultos',
        feathers = 'Plumas del Poderoso Águila', achievements = 'Logros', levels = 'Niveles completados',
        score = 'Puntuación total', playtime = 'Tiempo de juego', marmosets = 'Titíes explotados',
        cages = 'Jaulas rotas', crates = 'Cajas rotas', blocks = 'Bloques destruidos',
        unlocked = 'Desbloqueado', banner = 'Logro desbloqueado',
        complete = 'Completa %s', three_stars = 'Consigue tres estrellas en todos los niveles de %s',
        episode_score = 'Consigue %s puntos en %s', fruit = 'Encuentra las %s frutas ocultas de %s',
        fruit_salad = 'Encuentra frutas ocultas en %s episodios', star_count = 'Consigue %s estrellas',
        feather_count = 'Recoge %s plumas del Poderoso Águila', bonus = 'Consigue tres estrellas en %s niveles extra',
        blue = 'Conoce a los pájaros azules', yellow = 'Conoce al pájaro amarillo', black = 'Conoce al pájaro negro',
        white = 'Conoce al pájaro blanco', rio = 'Conoce a Blu y Perla', blu = 'Conoce a Blu en solitario',
        big = 'Conoce al gran pájaro rojo', boomerang = 'Conoce al pájaro bumerán', globe = 'Conoce al pájaro globo',
        eagle = 'Usa al Poderoso Águila', crate = 'Rompe %s cajas', flower = 'Rompe %s flores',
        block = 'Destruye %s bloques', lamp = 'Rompe %s lámparas', marmoset = 'Haz explotar %s titíes',
        cage = 'Rompe %s jaulas', credits = 'Mira los créditos', total = 'Alcanza una puntuación total de %s',
        friendly = 'Golpea un pájaro con otro pájaro', sniper = 'Golpea a un tití desde muy lejos',
        backwards = 'Lanza un pájaro hacia atrás %s veces', first_bird = 'Completa un nivel con el primer pájaro',
        gifted = 'Lanza %s pájaros seguidos que no hagan daño', minutes = 'Juega durante %s minutos',
        hours = 'Juega durante %s horas',
    },
    pt = {
        title = 'Estatísticas', progress = 'Progresso', stars = 'Estrelas', items = 'Itens escondidos',
        feathers = 'Penas da Poderosa Águia', achievements = 'Conquistas', levels = 'Níveis concluídos',
        score = 'Pontuação total', playtime = 'Tempo de jogo', marmosets = 'Saguis estourados',
        cages = 'Gaiolas quebradas', crates = 'Caixas quebradas', blocks = 'Blocos destruídos',
        unlocked = 'Desbloqueada', banner = 'Conquista desbloqueada',
        complete = 'Conclua %s', three_stars = 'Ganhe três estrelas em todos os níveis de %s',
        episode_score = 'Faça %s pontos em %s', fruit = 'Encontre as %s frutas escondidas de %s',
        fruit_salad = 'Encontre frutas escondidas em %s episódios', star_count = 'Ganhe %s estrelas',
        feather_count = 'Colete %s penas da Poderosa Águia', bonus = 'Ganhe três estrelas em %s níveis bônus',
        blue = 'Conheça os pássaros azuis', yellow = 'Conheça o pássaro amarelo', black = 'Conheça o pássaro preto',
        white = 'Conheça o pássaro branco', rio = 'Conheça Blu e Jade', blu = 'Conheça Blu sozinho',
        big = 'Conheça o grande pássaro vermelho', boomerang = 'Conheça o pássaro bumerangue',
        globe = 'Conheça o pássaro globo', eagle = 'Use a Poderosa Águia', crate = 'Quebre %s caixas',
        flower = 'Quebre %s flores', block = 'Destrua %s blocos', lamp = 'Quebre %s lâmpadas',
        marmoset = 'Estoure %s saguis', cage = 'Quebre %s gaiolas', credits = 'Assista aos créditos',
        total = 'Alcance uma pontuação total de %s', friendly = 'Acerte um pássaro com outro pássaro',
        sniper = 'Acerte um sagui de muito longe', backwards = 'Lance um pássaro para trás %s vezes',
        first_bird = 'Conclua um nível com o primeiro pássaro',
        gifted = 'Lance %s pássaros seguidos que não causem dano', minutes = 'Jogue por %s minutos',
        hours = 'Jogue por %s horas',
    },
}
function gameStatisticsLanguage()
    local l = tostring(_G.res.getLocale()):sub(1, 2)
    return gameStatisticsTexts[l] and l or 'en'
end
function gameStatisticsText(key)
    return gameStatisticsTexts[gameStatisticsLanguage()][key] or gameStatisticsTexts.en[key]
end
local SEPARATORS = {de = '.', it = '.', es = '.', pt = '.', fr = ' '}
function gameStatisticsNumber(n)
    local s = string.format('%.0f', math.floor(n or 0))
    local sep = SEPARATORS[gameStatisticsLanguage()] or ','
    return (s:reverse():gsub('(%d%d%d)', '%1' .. sep):reverse():gsub('^%' .. sep, ''))
end

-- The game's episode numbers and names.
local function episode_name(i)
    local ok, name = pcall(LevelQueryUtils.get_episode_title_text, i)
    return ok and tostring(name):gsub('%s+', ' '):gsub('^ ', ''):gsub(' $', '') or tostring(i)
end
local function levels(i)
    local ok, list = pcall(LevelQueryUtils.levelsInEpisode, i)
    return ok and list or {}
end
local function stars_in(i)
    local n = 0
    for _, l in ipairs(levels(i)) do n = n + (ProgressQueryUtils.numberOfStarsAchieved(l) or 0) end
    return n
end
local function passed_in(i)
    local n = 0
    for _, l in ipairs(levels(i)) do
        local h = highscores[l]
        if h and h.completed then n = n + 1 end
    end
    return n
end
-- The game's star count (A Star is Born ... Stellarvore): every episode but
-- Golden Beachball and the reward levels.
local function star_total()
    local n = 0
    for i, ep in ipairs(levelOrder.episodes) do
        if ep[1] ~= 'goldenbeachball' and ep[2] ~= 'goldenbeachball2' and ep[1] ~= 'rewardlevels' then
            n = n + (getEpisodeStars(i) or 0)
        end
    end
    return n
end
local function feather_total()
    local n = 0
    for i = 1, #levelOrder.episodes do n = n + (getEpisodeFeathersCount(i) or 0) end
    return n
end
-- Bonus levels with three stars, as Bonus Brilliance counts them.
local function bonus_total()
    local n = 0
    local three = function(l) return l and (ProgressQueryUtils.numberOfStarsAchieved(l) or 0) >= 3 end
    for i = 1, LevelQueryUtils.get_regular_episodes_count() do
        for _, l in pairs(LevelQueryUtils.bonusLevelsInEpisode(i) or {}) do if three(l) then n = n + 1 end end
        if three(LevelQueryUtils.eagleBonusLevelInEpisode(i)) then n = n + 1 end
    end
    for _, l in pairs(LevelQueryUtils.awardLevels() or {}) do if three(l) then n = n + 1 end end
    return n
end
local function fruits(i) return (getEpisodeCollectableCount(i)) end
local function fruit_episodes()
    local n = 0
    for i = 1, 7 do if fruits(i) > 0 then n = n + 1 end end
    return n
end
local function counter(key) return function() return settings[key] or 0 end end

-- The Mighty Eagle was bought (offline-shop sells it for coins).
function gameStatisticsEagle()
    return offlineShopConfigure ~= nil or (mightyEagle and mightyEagle.isEaglePurchased and mightyEagle.isEaglePurchased())
end

-- name, icon (ACHIEVEMENT_...), description key, its argument(s), progress
-- (value, target: numbers or functions); eagle: needs the Mighty Eagle.
local list = {}
local function add(name, icon, text, args, value, target, eagle)
    list[#list + 1] = {name = name, icon = 'ACHIEVEMENT_' .. icon, text = text, args = args or {},
                       value = value, target = target, eagle = eagle}
end
local EPISODES = {
    {'Breakout!', 'BREAKOUT', 'Warehousing Skills', 'WAREHOUSING_SKILLS', "Smugglers' Den - Score Addict", 2370000},
    {'Jungle Fever', 'JUNGLE_FEVER', 'Master of the Jungle', 'MASTER_OF_THE_JUNGLE', 'Jungle Escape - Score Addict', 2580000},
    {'Beach Life', 'BEACH_LIFE', 'Nicely Tanned!', 'NICELY_TANNED', 'Beach Volley - Score Addict', 3310000},
    {'Samba!', 'SAMBA', 'Carnival King', 'CARNIVAL_KING', 'Carnival Upheaval - Score Addict', 2850000},
    {'Going Somewhere?', 'GOING_SOMEWHERE', 'Airport Expert', 'AIRPORT_EXPERT', 'Airfield Chase - Score Addict', 3084000},
    {'Victory!', 'VICTORY', 'First Class', 'FIRST_CLASS', "Smugglers' Plane - Score Addict", 2890000},
}
for i, e in ipairs(EPISODES) do
    add(e[1], e[2], 'complete', {function() return episode_name(i) end}, function() return passed_in(i) end,
        function() return #levels(i) end)
    add(e[3], e[4], 'three_stars', {function() return episode_name(i) end}, function() return stars_in(i) end,
        function() return 3 * #levels(i) end)
    add(e[5], 'SCORE_ADDICT', 'episode_score', {e[6], function() return episode_name(i) end},
        function() return getEpisodeScore(i) end, e[6])
end
local LATER = {
    {7, 'Birds vs. Monkeys!', 'BIRDS_VS_MONKEYS'}, {8, 'Golden Hour', 'GOLDEN_HOUR'},
    {9, 'Party Animals', 'PARTY_ANIMALS'}, {10, 'Make a splash!', 'MAKE_A_SPLASH'},
    {11, 'Riverboat Captain', 'RIVERBOAT_CAPTAIN'}, {12, 'Bird Paradise', 'BIRD_PARADISE'},
}
for _, e in ipairs(LATER) do
    local i = e[1]
    add(e[2], e[3], 'complete', {function() return episode_name(i) end}, function() return passed_in(i) end,
        function() return #levels(i) end)
end
local FRUITS = {
    {'Pineapple Purist', 'PINEAPPLE_PURIST'}, {'Banana Fanatic', 'BANANA_FANATIC'}, {'Melon Mania', 'MELON_MANIA'},
    {'Papaya Perfection', 'PAPAYA_PERFECTION'}, {'Apple Appreciation', 'APPLE_APPRECIATION'},
    {'Mango Mastery', 'MANGO_MASTERY'},
}
for i, e in ipairs(FRUITS) do
    add(e[1], e[2], 'fruit', {15, function() return episode_name(i) end}, function() return fruits(i) end, 15)
end
add('Fruit Salad', 'FRUIT_SALAD', 'fruit_salad', {6}, fruit_episodes, 6)
for _, e in ipairs({{'A Star is Born', 'A_STAR_IS_BORN', 40}, {'Rising Star', 'RISING_STAR', 100},
                    {'Star Performer', 'STAR_PERFORMER', 150}, {'Super Star', 'SUPER_STAR', 215},
                    {'Mega Star', 'MEGA_STAR', 280}, {'Blue Giant', 'BLUE_GIANT', 310},
                    {'Stellarvore', 'STELLARVORE', 500}}) do
    add(e[1], e[2], 'star_count', {e[3]}, star_total, e[3])
end
add('Bonus Brilliance', 'BONUS_BRILLIANCE', 'bonus', {15}, bonus_total, 15)
for _, e in ipairs({{'Feather Picker Rio', 'FEATHERS_1', 30}, {'Feather Collector Rio', 'FEATHERS_2', 60},
                    {'Feather Gatherer Rio', 'FEATHERS_3', 120}}) do
    add(e[1], e[2], 'feather_count', {e[3]}, feather_total, e[3], true)
end
for _, e in ipairs({{'Clusterbirds', 'CLUSTERBIRDS', 'blue'}, {'Fast and Furious', 'FAST_AND_FURIOUS', 'yellow'},
                    {'Explosive Temperament', 'EXPLOSIVE_TEMPERAMENT', 'black'},
                    {'Scrambled Eggs', 'SCRAMBLED_EGGS', 'white'}, {'Birds of a Feather', 'BIRDS_OF_A_FEATHER', 'rio'},
                    {'Aerophobia', 'AEROPHOBIA', 'blu'}, {'Big is Beautiful', 'BIG_IS_BEAUTIFUL', 'big'},
                    {'There and back again', 'YOU_SPIN_ME_RIGHT_ROUND', 'boomerang'},
                    {'Global Phenomenon', 'GLOBAL_PHENOMENON', 'globe'}}) do
    add(e[1], e[2], e[3])
end
add('Aquiline Benefactor', 'UNLOCK_MIGHTY_EAGLE', 'eagle', nil, nil, nil, true)
add('Crate Hater', 'CRATE_HATER', 'crate', {200}, counter('cratesDestroyed'), 200)
add('Flower Power', 'FLOWER_POWER', 'flower', {100}, counter('flowersDestroyed'), 100)
add('Lights Out', 'LIGHTS_OUT', 'lamp', {10}, counter('lampsDestroyed'), 10)
add('Block Obliterator', 'BLOCK_OBLITERATOR', 'block', {10000}, counter('blocksDestroyed'), 10000)
add('Marmoset Terrorizer', 'MARMOSET_TERRORIZER', 'marmoset', {500}, counter('marmosetsDestroyed'), 500)
add('Marmoset Tormentor', 'MARMOSET_TORMENTOR', 'marmoset', {1500}, counter('marmosetsDestroyed'), 1500)
add('Marmoset Terminator', 'MARMOSET_TERMINATOR', 'marmoset', {2800}, counter('marmosetsDestroyed'), 2800)
add('Avian Liberator', 'AVIAN_LIBERATOR', 'cage', {700}, counter('birdCagesDestroyed'), 700)
add('Free Bird', 'FREE_BIRD', 'cage', {1200}, counter('birdCagesDestroyed'), 1200)
add('No Cage Can Hold Me!', 'NO_CAGE_CAN_HOLD_ME', 'cage', {2000}, counter('birdCagesDestroyed'), 2000)
add('Half-a-Millionaire', 'HALF_A_MILLIONAIRE', 'total', {500000})
add('6 Million Dollar Bird', '6_MILLION_DOLLAR_BIRD', 'total', {6000000})
add('Multi-Millionaire', 'MULTI_MILLIONAIRE', 'total', {17500000})
add('Credit Where Credit is Due', 'CREDIT_WHERE_CREDIT_IS_DUE', 'credits')
add('Friendly Fire', 'FRIENDLY_FIRE', 'friendly')
add('Sniper', 'SNIPER', 'sniper')
add('Lateral Thinker', 'LATERAL_THINKER', 'backwards', {3},
    function() return settings.achievements and settings.achievements.backwardsBirdCount or 0 end, 3)
add('Sharp Shooter', 'SHARP_SHOOTER', 'first_bird')
add('Gifted', 'GIFTED', 'gifted', {5})
add('Enthusiastic Player', 'ENTHUSIASTIC_PLAYER', 'minutes', {30},
    function() return math.floor((settings.playtime or 0) / 60) end, 30)
add('Dedicated Player', 'DEDICATED_PLAYER', 'hours', {2},
    function() return math.floor((settings.playtime or 0) / 3600) end, 2)
gameStatisticsAchievements = list
local BY_NAME = {}
for _, a in ipairs(list) do BY_NAME[a.name] = a end

-- Listed: every achievement, the Mighty Eagle's once it can be had.
function gameStatisticsList()
    local eagle, out = gameStatisticsEagle(), {}
    for _, a in ipairs(list) do if eagle or not a.eagle then out[#out + 1] = a end end
    return out
end
function gameStatisticsUnlocked(a)
    return settings.gameStatisticsUnlocked ~= nil and settings.gameStatisticsUnlocked[a.name] == true
end
-- Description, value, target.
function gameStatisticsDescribe(a)
    local args = {}
    for i, v in ipairs(a.args) do
        if type(v) == 'function' then v = v() elseif type(v) == 'number' then v = gameStatisticsNumber(v) end
        args[i] = tostring(v)
    end
    local text = gameStatisticsText(a.text)
    local n = 0
    text = text:gsub('%%s', function() n = n + 1 return (args[n] or ''):gsub('%%', '%%%%') end)
    local value
    if a.value then
        local ok, v = pcall(a.value)
        value = ok and tonumber(v) or 0
    end
    local target = a.target
    if type(target) == 'function' then target = target() end
    return text, value, target
end

-- The achievement icons come in one sheet for 480x320 only, which no asset
-- group loads at this resolution.
function gameStatisticsLoadIcons()
    local w = _G.res.getSpriteBounds('ACHIEVEMENT_BREAKOUT')
    if not w or w == 0 then pcall(_G.res.createSpriteSheet, 'images/480x320/ACHIEVEMENTS_SHEET_1.dat') end
end

-- Unlocks: the game queues them for Game Center (addToAchievementUnlockQueue),
-- and only with it; here they are stored in settings.gameStatisticsUnlocked
-- and shown with the game's toast.
local silent = false
local function toast(a)
    gameStatisticsLoadIcons()
    local t = menuengine.model.toast.Toast:new({priority = 1, title = gameStatisticsText('banner'),
                                                text = a.name, icon = a.icon})
    menuengine.instance.toastManager:push(t)
end
local FAKE = {achievements = {}}
local queue = addToAchievementUnlockQueue
function addToAchievementUnlockQueue(name, ...)
    if gameCenterEnabled and gameCenter and gameCenter ~= FAKE then return queue(name, ...) end
    local a = BY_NAME[name]
    if not a or not settings then return end
    if not settings.gameStatisticsUnlocked and not silent then gameStatisticsCatchUp() end
    settings.gameStatisticsUnlocked = settings.gameStatisticsUnlocked or {}
    if settings.gameStatisticsUnlocked[name] then return end
    settings.gameStatisticsUnlocked[name] = true
    saveLuaFileWrapper('settings.lua', 'settings', true)
    if not silent then pcall(toast, a) end
end

-- The checks run only with Game Center; they run with a stand-in.
local function ungated(check)
    return function(...)
        if gameCenterEnabled and gameCenter ~= FAKE then return check(...) end
        local enabled, center = gameCenterEnabled, gameCenter
        gameCenterEnabled, gameCenter = true, FAKE
        local results = {pcall(check, ...)}
        gameCenterEnabled, gameCenter = enabled, center
        if not results[1] then error(results[2], 0) end
        return unpack(results, 2)
    end
end
for _, name in ipairs({'checkForAchievements', 'checkForPlayTimeAchievements', 'checkForBlockAchievements',
                       'checkForMiscAchievements', 'checkForFruitAchievements', 'checkForEpisodeAchievements',
                       'checkForMarmosetAchievements', 'checkForBirdCageAchievements',
                       'checkForTutorialAchievements'}) do
    if getfenv(1)[name] then getfenv(1)[name] = ungated(getfenv(1)[name]) end
end
-- Achievements met before: stored without a toast.
function gameStatisticsCatchUp()
    if not settings then return end
    silent = true
    pcall(checkForAchievements)
    silent = false
    settings.gameStatisticsUnlocked = settings.gameStatisticsUnlocked or {}
end
local function check()
    if settings and not settings.gameStatisticsUnlocked then gameStatisticsCatchUp() end
    pcall(checkForAchievements, true)
end

-- The level end checks with Game Center or Xbox only.
local level_end = gotoLevelEnding
function gotoLevelEnding(...)
    local results = {level_end(...)}
    if not (gameCenterSupported or XBOXSupported) then check() end
    return unpack(results)
end

-- removeBlocks counts lamps, marmosets, bird cages, crates, flowers and blocks
-- on iPhone and iPad only; here it counts them on every device, by the same
-- rules, before the game removes the blocks.
local MATERIALS = {lamp = 'lampsDestroyed', lanternLight = 'lampsDestroyed', monkey = 'marmosetsDestroyed',
                   cage = 'birdCagesDestroyed', crate = 'cratesDestroyed', flower = 'flowersDestroyed'}
local remove = removeBlocks
function removeBlocks(...)
    if deviceModel == 'iphone' or deviceModel == 'ipad' or not objects.world or not settings then
        return remove(...)
    end
    local counted = false
    if not isEagleButtonPressed() then
        for _, b in pairs(deadBlocks) do
            if b and b.strength and b.strength <= 0 and b.destroyed ~= true and b.material
                and not (b.rocket ~= nil and b.deathTimerFull == nil) then
                local key = MATERIALS[b.material]
                if key then settings[key] = (settings[key] or 0) + 1 end
                settings.blocksDestroyed = (settings.blocksDestroyed or 0) + 1
                counted = true
            end
        end
    end
    local results = {remove(...)}
    if counted then
        if not settings.gameStatisticsUnlocked then gameStatisticsCatchUp() end
        pcall(checkForBlockAchievements, true)
        pcall(checkForMarmosetAchievements, true)
        pcall(checkForBirdCageAchievements, true)
    end
    return unpack(results)
end

end
)";

// The page, a window like the game's message popups.
constexpr const char* kPage = R"(
local sg, T = gamelua.scenegraph, gamelua.Transformation
local function text(key) return gamelua.gameStatisticsText(key) end
local function number(n) return gamelua.gameStatisticsNumber(n) end

local function label(items, s, x, y, hanchor, max_width, scale, font)
    local t = sg.makeText(tostring(s), font or 'FONT_EP_SCORE')
    t.hanchor, t.vanchor = hanchor or 'LEFT', 'TOP'
    scale = scale or 1
    local w = t:getWidth() * scale
    if max_width and w > max_width then scale = scale * max_width / w end
    t.transformation = T.new(scale)
    t.x, t.y = x, y
    t.scaledWidth = t:getWidth() * scale
    items[#items + 1] = t
    return t
end
local function rect(items, x, y, w, h, r, gr, b, a)
    local box = sg.makeRectangle({width = w, height = h}, {r = r, g = gr, b = b, a = a or 1})
    -- Rectangles are placed by their centre.
    box.x, box.y = x + w / 2, y + h / 2
    items[#items + 1] = box
    return box
end
local function sprite(items, name, x, y, scale, alpha)
    local s = sg.makeSprite(name)
    s.transformation = T.new(scale or 1)
    s.x, s.y = x, y
    if alpha then s:setAlpha(alpha) end
    items[#items + 1] = s
    return s
end
local function group(items)
    return {x = 0, y = 0, transformation = T.identity(), items = items or {},
            handleInput = sg.relayInputShopWithSelfScaling}
end

-- Progress parts: stars, hidden items, feathers (with the Mighty Eagle),
-- achievements.
local function parts()
    local L, P = gamelua.LevelQueryUtils, gamelua.ProgressQueryUtils
    local eagle = gamelua.gameStatisticsEagle()
    local stars, stars_max, items, items_max, feathers, feathers_max = 0, 0, 0, 0, 0, 0
    local seen = {}
    local function star_level(l)
        if l and not seen[l] then
            seen[l] = true
            stars, stars_max = stars + (P.numberOfStarsAchieved(l) or 0), stars_max + 3
        end
    end
    for i = 1, L.get_regular_episodes_count() do
        local levels = L.levelsInEpisode(i) or {}
        for _, l in ipairs(levels) do star_level(l) end
        for _, l in pairs(L.bonusLevelsInEpisode(i) or {}) do star_level(l) end
        if eagle then
            star_level(L.eagleBonusLevelInEpisode(i))
            feathers, feathers_max = feathers + (P.featherCount(i) or 0), feathers_max + #levels
        end
        -- Fruits and other hidden items; Treasure Hunt hides coins.
        if L.goldenFruitInEpisode(i) and L.goldenFruitInEpisode(i) ~= 'coin' then
            local total = L.getEpisodeCollectibleCount(i) or 0
            items, items_max = items + math.min((gamelua.getEpisodeCollectableCount(i)), total), items_max + total
        end
    end
    for _, l in pairs(L.awardLevels() or {}) do star_level(l) end
    local list, count = gamelua.gameStatisticsList(), 0
    for _, a in ipairs(list) do if gamelua.gameStatisticsUnlocked(a) then count = count + 1 end end
    local out = {{text('stars'), stars, stars_max}, {text('items'), items, items_max}}
    if eagle then out[#out + 1] = {text('feathers'), feathers, feathers_max} end
    out[#out + 1] = {text('achievements'), count, #list}
    return out
end
local function counters()
    local L = gamelua.LevelQueryUtils
    local s = gamelua.settings
    local done, total, score = 0, 0, 0
    for i = 1, L.get_regular_episodes_count() do
        for _, l in ipairs(L.levelsInEpisode(i) or {}) do
            total = total + 1
            local h = gamelua.highscores[l]
            if h and h.completed then done = done + 1 end
        end
        score = score + (gamelua.getEpisodeScore(i) or 0)
    end
    pcall(gamelua.calculatePlaytime)
    local minutes = math.floor((s.playtime or 0) / 60)
    local FORMATS = {de = '%d Std. %02d Min.'}
    local format = FORMATS[gamelua.gameStatisticsLanguage()] or '%d h %02d min'
    return {
        {text('levels'), number(done) .. ' / ' .. number(total)},
        {text('score'), number(score)},
        {text('playtime'), format:format(math.floor(minutes / 60), minutes % 60)},
        {text('marmosets'), number(s.marmosetsDestroyed or 0)},
        {text('cages'), number(s.birdCagesDestroyed or 0)},
        {text('crates'), number(s.cratesDestroyed or 0)},
        {text('blocks'), number(s.blocksDestroyed or 0)},
    }
end

local PER_PAGE = 6

local function make_popup()
    gamelua.gameStatisticsCatchUp()
    gamelua.gameStatisticsLoadIcons()
    local ui = gamelua.g_ui_scale
    local W, H = gamelua.screenWidth / ui * 0.92, gamelua.screenHeight / ui * 0.86
    -- Content coordinates are in reference units (the window scales them).
    local PW, PH = W * 0.94, H * 0.9
    local items = {}
    local top = -PH / 2
    label(items, text('title'), 0, top - 4, 'HCENTER', PW * 0.5, 0.8, 'FONT_EP_TITLE')

    -- left: progress, parts, counters
    local x0, x1 = -PW / 2 + 6, -PW * 0.04
    local list = parts()
    local sum = 0
    for _, p in ipairs(list) do sum = sum + (p[3] > 0 and math.min(p[2] / p[3], 1) or 1) end
    local total = sum / #list
    local y = top + 38
    local percent = label(items, string.format('%d %%', math.floor(total * 100)), x1, y, 'RIGHT', nil, 0.8)
    label(items, text('progress'), x0, y, 'LEFT', x1 - x0 - percent.scaledWidth - 10, 0.8)
    y = y + 26
    rect(items, x0, y, x1 - x0, 14, 0, 0, 0, 0.5)
    rect(items, x0 + 2, y + 2, math.max((x1 - x0 - 4) * total, 0), 10, 1, 0.8, 0.1, 1)
    y = y + 24
    local row = 23
    for _, p in ipairs(list) do
        local v = label(items, number(p[2]) .. ' / ' .. number(p[3]), x1, y, 'RIGHT', nil, 0.65)
        label(items, p[1], x0, y, 'LEFT', x1 - x0 - v.scaledWidth - 8, 0.65)
        y = y + row
    end
    y = y + 10
    for _, c in ipairs(counters()) do
        local v = label(items, c[2], x1, y, 'RIGHT', nil, 0.65)
        label(items, c[1], x0, y, 'LEFT', x1 - x0 - v.scaledWidth - 8, 0.65)
        y = y + row
    end

    -- right: the achievements, a page at a time
    local area = {x0 = PW * 0.03, x1 = PW / 2 - 6, y = top + 38}
    local achievements = gamelua.gameStatisticsList()
    local unlocked = 0
    for _, a in ipairs(achievements) do if gamelua.gameStatisticsUnlocked(a) then unlocked = unlocked + 1 end end
    local count = label(items, number(unlocked) .. ' / ' .. number(#achievements), area.x1, area.y, 'RIGHT', nil, 0.8)
    label(items, text('achievements'), area.x0, area.y, 'LEFT', area.x1 - area.x0 - count.scaledWidth - 10, 0.8)
    local pages = math.max(1, math.ceil(#achievements / PER_PAGE))
    local rows = group()
    items[#items + 1] = rows
    local page_text
    local page = 1
    local function show(p)
        page = math.max(1, math.min(p, pages))
        local r = {}
        local ry = area.y + 32
        local text_x = area.x0 + 48
        for i = (page - 1) * PER_PAGE + 1, math.min(page * PER_PAGE, #achievements) do
            local a = achievements[i]
            local done = gamelua.gameStatisticsUnlocked(a)
            local description, value, target = gamelua.gameStatisticsDescribe(a)
            sprite(r, a.icon, area.x0 + 20, ry + 20, 1.4, not done and 0.3 or nil)
            local status
            if done then
                status = text('unlocked')
            elseif value and target then
                status = number(math.min(value, target)) .. ' / ' .. number(target)
            end
            local status_w = 0
            if status then status_w = label(r, status, area.x1, ry + 2, 'RIGHT', nil, 0.55).scaledWidth + 8 end
            label(r, a.name, text_x, ry, 'LEFT', area.x1 - text_x - status_w, 0.65)
            label(r, description, text_x, ry + 21, 'LEFT', area.x1 - text_x, 0.5)
            ry = ry + 47
        end
        rows.items = r
        if page_text then page_text.text = page .. ' / ' .. pages end
    end
    local by = PH / 2 - 16
    local mid = (area.x0 + area.x1) / 2
    page_text = label(items, '1 / ' .. pages, mid, by - 9, 'HCENTER', nil, 0.65)
    local function arrow(x, flip, step)
        local icon = sg.makeSprite('ICON_NEXT')
        if flip then icon.transformation = T.new(-1, 1) end
        local b = makeButton({background = sg.makeSprite('BTN_BG_ORANGE'), icon = icon,
                              scale = {x = 0.45, y = 0.45}, action = function() show(page + step) end})
        b.x, b.y = x, by
        items[#items + 1] = b
    end
    arrow(area.x0 + 24, true, -1)
    arrow(area.x1 - 24, false, 1)
    show(1)

    -- The window scales what it draws by ui, but not the input.
    local content = {x = 0, y = 0, items = items,
                     handleInput = function(self, x, y, ...)
                         return sg.relayInputShopWithSelfScaling(self, x and x / ui, y and y / ui, ...)
                     end,
                     width = function() return W end, height = function() return H end, layout = function() end}
    local close = function()
        gamelua.eventManager:send({id = gamelua.events.EID_POPUP_CLOSE, page = gamelua.menuengine.model.popup.PAGE_NAME})
    end
    return makeNotificationWindow(W, H, close, content, WindowTheme.POPUP_DARK, false, ui)
end
function gameStatisticsOpen()
    local me = gamelua.menuengine
    me.instance.popupManager:push(me.model.popup.Popup:new({content = make_popup(), show_shade = true}))
end
)";

// The button, top left (the settings button is bottom right).
constexpr const char* kMenu = R"(
local init = MainMenuInitialPage.init
function MainMenuInitialPage:init(...)
    local results = {init(self, ...)}
    local g = gamelua
    local s = g.g_ui_scale
    self:insertItem('statisticsButton', g.menus.buttonFactory.CompositeButton:new({
        startScaleX = s, startScaleY = s, endScaleX = 1.1 * s, endScaleY = 1.1 * s,
        buttonName = 'BTN_BG_ORANGE', spriteName = 'ICON_ACHIEVEMENTS', activateOnRelease = true, selectable = true,
        action = function() g.Components.gameStatisticsOpen() end,
    }))
    return unpack(results)
end
local layout = MainMenuInitialPage.layout
function MainMenuInitialPage:layout(...)
    local results = {layout(self, ...)}
    local b = self.items and self.items.statisticsButton
    if b then
        local w, h = _G.res.getSpriteBounds(b.button.sprite)
        local s = gamelua.g_ui_scale
        b.x, b.y = w * s / 2, h * s / 2
    end
    return unpack(results)
end
)";

class GameStatistics final : public slingshot::Mod {
public:
    GameStatistics() {
        using slingshot::mods::append_lua;
        append_lua("data/scripts/gamelogic.lua", kGameLogic);
        append_lua("data/scripts/Components.lua", kPage);
        append_lua("data/scripts/menu/MainMenuInitialPage.lua", kMenu);
    }
};

}  // namespace

SLINGSHOT_MOD(GameStatistics, "game-statistics",
              "Statistics, progress and achievements on a page of the main menu; unlocks show as a toast");
