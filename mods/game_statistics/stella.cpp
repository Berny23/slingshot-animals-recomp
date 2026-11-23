// SPDX-License-Identifier: GPL-3.0-or-later
// A trophy button on the island opens a page with the progress to 100 %, the
// game's counters and its achievements; unlocks show in a banner.
//
// The game unlocks and stores its 31 achievements offline (AchievementCounters,
// AchievementDefinitions, SettingsWrapper.registerAchievement); only Google
// Play and Xbox showed them. Their names and descriptions came from those
// services, so the mod has its own, in the game's 10 languages, written with
// the glyphs of the game's fonts. Left out: the 3 social ones (Facebook
// friends' scores), and "Album Maker" (30 outfits) unless offline-shop opens
// the gift boxes' outfits. The island's trophy button (btnAchievements, shown
// only on Xbox) opens the page.
//
// The progress is the mean of stars, bird outfits, collection items and
// achievements.
//
// The achievements also go to Recomp Achievements (common/recomp_achievements.hpp).
#include <string>

#include "common/game_scripts.hpp"
#include "common/recomp_achievements.hpp"
#include "common/statistics.hpp"
#include "mod.hpp"

namespace {

// The texts; the playtime, its units and "Achievement unlocked!" are the game's.
constexpr const char* kTexts = R"(
local TEXTS = {
    en = {
        title = 'Statistics', progress = 'Game progress', stars = 'Stars', skins = 'Bird outfits',
        items = 'Collection items', achievements = 'Achievements', levels = 'Levels completed',
        score = 'Total score', birds = 'Birds flung', pigs = 'Pigs popped', earned = 'Coins earned',
        spent = 'Coins spent', player_level = 'Player level', unlocked = 'Unlocked',
        STAR_GAZER = {'Star Gazer', 'Earn %s stars'},
        STAR_REACHER = {'Star Reacher', 'Earn %s stars'},
        STAR_TREKER = {'Star Trekker', 'Earn %s stars'},
        STAR_BOUNDER = {'Star Bounder', 'Earn %s stars'},
        COIN_GRABBER = {'Coin Grabber', 'Earn %s coins'},
        HEAVY_PURSE = {'Heavy Purse', 'Earn %s coins'},
        BIG_SPENDER = {'Big Spender', 'Spend %s coins'},
        SNAPSHOT = {'Snapshot', 'Unlock %s bird outfit'},
        HEADSHOT = {'Headshot', 'Unlock %s bird outfits'},
        CAMERA_ROLL = {'Camera Roll', 'Unlock %s bird outfits'},
        PAPARAZZI = {'Paparazzi', 'Unlock %s bird outfits'},
        ALBUM_MAKER = {'Album Maker', 'Unlock %s bird outfits'},
        PIGGINING = {'Pig-ginner', 'Pop %s pigs'},
        PIG_OUT = {'Pig Out', 'Pop %s pigs'},
        TREE_TOP_COMPLETE = {'Treetop Hero', 'Complete all levels of chapter 1'},
        TREE_TOP_HIGH_TOTAL_SCORE = {'Treetop Tycoon', 'Score %s points in chapter 1'},
        BEACH_TOP_COMPLETE = {'Beach Hero', 'Complete all levels of chapter 2'},
        BEACH_HIGH_TOTAL_SCORE = {'Beach Tycoon', 'Score %s points in chapter 2'},
        N_THREE_STARRED_LEVELS = {'Three-Star Talent', 'Get three stars on %s levels'},
        N_THREE_STARRED_LEVELS_2 = {'Three-Star Master', 'Get three stars on %s levels'},
        PARCOR_KID = {'Parkour Kid', 'Make Stella bounce %s times'},
        LEAPS_AND_BOUNDS = {'Leaps and Bounds', 'Make Stella bounce %s times'},
        HIGH_PLAY_TIME_1 = {'Warming Up', 'Play for %s hour'},
        HIGH_PLAY_TIME_5 = {'Regular', 'Play for %s hours'},
        HIGH_PLAY_TIME_10 = {'Devoted Fan', 'Play for %s hours'},
        N_DRUM_BOUNCES_WITH_ONE_BIRD = {'Drum Roll', 'Bounce one bird off drums %s times'},
        HAMMER_TIME = {'Hammer Time', 'Use a power-up'},
        SMASHY_SMASHY = {'Smashy Smashy', 'Use %s power-ups'},
    },
    de = {
        title = 'Statistik', progress = 'Spielfortschritt', stars = 'Sterne', skins = 'Vogel-Outfits',
        items = 'Sammelobjekte', achievements = 'Erfolge', levels = 'Abgeschlossene Level',
        score = 'Gesamtpunktzahl', birds = 'Geschleuderte Vögel', pigs = 'Geplatzte Schweine',
        earned = 'Verdiente Münzen', spent = 'Ausgegebene Münzen', player_level = 'Spielerlevel',
        unlocked = 'Freigeschaltet',
        STAR_GAZER = {'Sternengucker', 'Verdiene %s Sterne'},
        STAR_REACHER = {'Sternengreifer', 'Verdiene %s Sterne'},
        STAR_TREKER = {'Sternenwanderer', 'Verdiene %s Sterne'},
        STAR_BOUNDER = {'Sternenspringer', 'Verdiene %s Sterne'},
        COIN_GRABBER = {'Münzsammler', 'Verdiene %s Münzen'},
        HEAVY_PURSE = {'Volle Börse', 'Verdiene %s Münzen'},
        BIG_SPENDER = {'Großzügig', 'Gib %s Münzen aus'},
        SNAPSHOT = {'Schnappschuss', 'Schalte %s Vogel-Outfit frei'},
        HEADSHOT = {'Porträt', 'Schalte %s Vogel-Outfits frei'},
        CAMERA_ROLL = {'Filmrolle', 'Schalte %s Vogel-Outfits frei'},
        PAPARAZZI = {'Paparazzi', 'Schalte %s Vogel-Outfits frei'},
        ALBUM_MAKER = {'Fotoalbum', 'Schalte %s Vogel-Outfits frei'},
        PIGGINING = {'Schweinestart', 'Lass %s Schweine platzen'},
        PIG_OUT = {'Schweinerei', 'Lass %s Schweine platzen'},
        TREE_TOP_COMPLETE = {'Baumkronenheld', 'Schließe alle Level von Kapitel 1 ab'},
        TREE_TOP_HIGH_TOTAL_SCORE = {'Baumkronenkönig', 'Erziele %s Punkte in Kapitel 1'},
        BEACH_TOP_COMPLETE = {'Strandheld', 'Schließe alle Level von Kapitel 2 ab'},
        BEACH_HIGH_TOTAL_SCORE = {'Strandkönig', 'Erziele %s Punkte in Kapitel 2'},
        N_THREE_STARRED_LEVELS = {'Drei-Sterne-Talent', 'Hol drei Sterne in %s Leveln'},
        N_THREE_STARRED_LEVELS_2 = {'Drei-Sterne-Meister', 'Hol drei Sterne in %s Leveln'},
        PARCOR_KID = {'Parkour-Kind', 'Lass Stella %s-mal abprallen'},
        LEAPS_AND_BOUNDS = {'Hoch hinaus', 'Lass Stella %s-mal abprallen'},
        HIGH_PLAY_TIME_1 = {'Aufgewärmt', 'Spiele %s Stunde'},
        HIGH_PLAY_TIME_5 = {'Stammgast', 'Spiele %s Stunden'},
        HIGH_PLAY_TIME_10 = {'Treuer Fan', 'Spiele %s Stunden'},
        N_DRUM_BOUNCES_WITH_ONE_BIRD = {'Trommelwirbel', 'Lass einen Vogel %s-mal von Trommeln abprallen'},
        HAMMER_TIME = {'Hammerzeit', 'Setze ein Power-up ein'},
        SMASHY_SMASHY = {'Kurz und klein', 'Setze %s Power-ups ein'},
    },
    fr = {
        title = 'Statistiques', progress = 'Progression', stars = 'Étoiles', skins = 'Tenues d\'oiseaux',
        items = 'Objets de collection', achievements = 'Succès', levels = 'Niveaux terminés', score = 'Score total',
        birds = 'Oiseaux lancés', pigs = 'Cochons éclatés', earned = 'Pièces gagnées', spent = 'Pièces dépensées',
        player_level = 'Niveau du joueur', unlocked = 'Débloqué',
        STAR_GAZER = {'Observateur d\'étoiles', 'Gagnez %s étoiles'},
        STAR_REACHER = {'Attrape-étoiles', 'Gagnez %s étoiles'},
        STAR_TREKER = {'Voyageur des étoiles', 'Gagnez %s étoiles'},
        STAR_BOUNDER = {'Sauteur étoilé', 'Gagnez %s étoiles'},
        COIN_GRABBER = {'Ramasse-pièces', 'Gagnez %s pièces'},
        HEAVY_PURSE = {'Bourse pleine', 'Gagnez %s pièces'},
        BIG_SPENDER = {'Dépensier', 'Dépensez %s pièces'},
        SNAPSHOT = {'Instantané', 'Débloquez %s tenue d\'oiseau'},
        HEADSHOT = {'Portrait', 'Débloquez %s tenues d\'oiseaux'},
        CAMERA_ROLL = {'Pellicule', 'Débloquez %s tenues d\'oiseaux'},
        PAPARAZZI = {'Paparazzi', 'Débloquez %s tenues d\'oiseaux'},
        ALBUM_MAKER = {'Album photo', 'Débloquez %s tenues d\'oiseaux'},
        PIGGINING = {'Premiers cochons', 'Éclatez %s cochons'},
        PIG_OUT = {'Cochonnerie', 'Éclatez %s cochons'},
        TREE_TOP_COMPLETE = {'Héros de la cime', 'Terminez tous les niveaux du chapitre 1'},
        TREE_TOP_HIGH_TOTAL_SCORE = {'Roi de la cime', 'Marquez %s points dans le chapitre 1'},
        BEACH_TOP_COMPLETE = {'Héros de la plage', 'Terminez tous les niveaux du chapitre 2'},
        BEACH_HIGH_TOTAL_SCORE = {'Roi de la plage', 'Marquez %s points dans le chapitre 2'},
        N_THREE_STARRED_LEVELS = {'Talent trois étoiles', 'Obtenez trois étoiles dans %s niveaux'},
        N_THREE_STARRED_LEVELS_2 = {'Maître trois étoiles', 'Obtenez trois étoiles dans %s niveaux'},
        PARCOR_KID = {'As du parkour', 'Faites rebondir Stella %s fois'},
        LEAPS_AND_BOUNDS = {'Bonds de géant', 'Faites rebondir Stella %s fois'},
        HIGH_PLAY_TIME_1 = {'Échauffement', 'Jouez pendant %s heure'},
        HIGH_PLAY_TIME_5 = {'Habitué', 'Jouez pendant %s heures'},
        HIGH_PLAY_TIME_10 = {'Fan dévoué', 'Jouez pendant %s heures'},
        N_DRUM_BOUNCES_WITH_ONE_BIRD = {'Roulement de tambour', 'Faites rebondir un oiseau %s fois sur des tambours'},
        HAMMER_TIME = {'C\'est l\'heure du marteau', 'Utilisez un bonus'},
        SMASHY_SMASHY = {'En mille morceaux', 'Utilisez %s bonus'},
    },
    it = {
        title = 'Statistiche', progress = 'Progressi', stars = 'Stelle', skins = 'Costumi degli uccelli',
        items = 'Oggetti da collezione', achievements = 'Obbiettivi', levels = 'Livelli completati',
        score = 'Punteggio totale', birds = 'Uccelli lanciati', pigs = 'Maiali scoppiati',
        earned = 'Monete guadagnate', spent = 'Monete spese', player_level = 'Livello giocatore',
        unlocked = 'Sbloccato',
        STAR_GAZER = {'Guarda le stelle', 'Guadagna %s stelle'},
        STAR_REACHER = {'Acchiappastelle', 'Guadagna %s stelle'},
        STAR_TREKER = {'Viaggiatore stellare', 'Guadagna %s stelle'},
        STAR_BOUNDER = {'Salto stellare', 'Guadagna %s stelle'},
        COIN_GRABBER = {'Raccogli monete', 'Guadagna %s monete'},
        HEAVY_PURSE = {'Borsa piena', 'Guadagna %s monete'},
        BIG_SPENDER = {'Spendaccione', 'Spendi %s monete'},
        SNAPSHOT = {'Istantanea', 'Sblocca %s costume'},
        HEADSHOT = {'Ritratto', 'Sblocca %s costumi'},
        CAMERA_ROLL = {'Rullino', 'Sblocca %s costumi'},
        PAPARAZZI = {'Paparazzi', 'Sblocca %s costumi'},
        ALBUM_MAKER = {'Album di foto', 'Sblocca %s costumi'},
        PIGGINING = {'Primi maiali', 'Fai scoppiare %s maiali'},
        PIG_OUT = {'Porcheria', 'Fai scoppiare %s maiali'},
        TREE_TOP_COMPLETE = {'Eroe della chioma', 'Completa tutti i livelli del capitolo 1'},
        TREE_TOP_HIGH_TOTAL_SCORE = {'Re della chioma', 'Ottieni %s punti nel capitolo 1'},
        BEACH_TOP_COMPLETE = {'Eroe della spiaggia', 'Completa tutti i livelli del capitolo 2'},
        BEACH_HIGH_TOTAL_SCORE = {'Re della spiaggia', 'Ottieni %s punti nel capitolo 2'},
        N_THREE_STARRED_LEVELS = {'Talento a tre stelle', 'Ottieni tre stelle in %s livelli'},
        N_THREE_STARRED_LEVELS_2 = {'Maestro a tre stelle', 'Ottieni tre stelle in %s livelli'},
        PARCOR_KID = {'Ragazza parkour', 'Fai rimbalzare Stella %s volte'},
        LEAPS_AND_BOUNDS = {'Passi da gigante', 'Fai rimbalzare Stella %s volte'},
        HIGH_PLAY_TIME_1 = {'Riscaldamento', 'Gioca per %s ora'},
        HIGH_PLAY_TIME_5 = {'Habitué', 'Gioca per %s ore'},
        HIGH_PLAY_TIME_10 = {'Fan devoto', 'Gioca per %s ore'},
        N_DRUM_BOUNCES_WITH_ONE_BIRD = {'Rullo di tamburi', 'Fai rimbalzare un uccello %s volte sui tamburi'},
        HAMMER_TIME = {'Ora del martello', 'Usa un power-up'},
        SMASHY_SMASHY = {'A pezzi', 'Usa %s power-up'},
    },
    es = {
        title = 'Estadísticas', progress = 'Progreso', stars = 'Estrellas', skins = 'Atuendos de pájaros',
        items = 'Objetos de colección', achievements = 'Logros', levels = 'Niveles completados',
        score = 'Puntuación total', birds = 'Pájaros lanzados', pigs = 'Cerdos explotados',
        earned = 'Monedas ganadas', spent = 'Monedas gastadas', player_level = 'Nivel de jugador',
        unlocked = 'Desbloqueado',
        STAR_GAZER = {'Observador de estrellas', 'Gana %s estrellas'},
        STAR_REACHER = {'Atrapaestrellas', 'Gana %s estrellas'},
        STAR_TREKER = {'Viajero estelar', 'Gana %s estrellas'},
        STAR_BOUNDER = {'Salto estelar', 'Gana %s estrellas'},
        COIN_GRABBER = {'Recogemonedas', 'Gana %s monedas'},
        HEAVY_PURSE = {'Bolsa llena', 'Gana %s monedas'},
        BIG_SPENDER = {'Derrochador', 'Gasta %s monedas'},
        SNAPSHOT = {'Instantánea', 'Desbloquea %s atuendo'},
        HEADSHOT = {'Retrato', 'Desbloquea %s atuendos'},
        CAMERA_ROLL = {'Carrete', 'Desbloquea %s atuendos'},
        PAPARAZZI = {'Paparazzi', 'Desbloquea %s atuendos'},
        ALBUM_MAKER = {'Álbum de fotos', 'Desbloquea %s atuendos'},
        PIGGINING = {'Primeros cerdos', 'Explota %s cerdos'},
        PIG_OUT = {'Cerdada', 'Explota %s cerdos'},
        TREE_TOP_COMPLETE = {'Héroe de la copa', 'Completa todos los niveles del capítulo 1'},
        TREE_TOP_HIGH_TOTAL_SCORE = {'Rey de la copa', 'Consigue %s puntos en el capítulo 1'},
        BEACH_TOP_COMPLETE = {'Héroe de la playa', 'Completa todos los niveles del capítulo 2'},
        BEACH_HIGH_TOTAL_SCORE = {'Rey de la playa', 'Consigue %s puntos en el capítulo 2'},
        N_THREE_STARRED_LEVELS = {'Talento de tres estrellas', 'Consigue tres estrellas en %s niveles'},
        N_THREE_STARRED_LEVELS_2 = {'Maestro de tres estrellas', 'Consigue tres estrellas en %s niveles'},
        PARCOR_KID = {'Chica parkour', 'Haz rebotar a Stella %s veces'},
        LEAPS_AND_BOUNDS = {'A pasos agigantados', 'Haz rebotar a Stella %s veces'},
        HIGH_PLAY_TIME_1 = {'Calentamiento', 'Juega durante %s hora'},
        HIGH_PLAY_TIME_5 = {'Habitual', 'Juega durante %s horas'},
        HIGH_PLAY_TIME_10 = {'Fan incondicional', 'Juega durante %s horas'},
        N_DRUM_BOUNCES_WITH_ONE_BIRD = {'Redoble de tambor', 'Haz rebotar un pájaro %s veces en tambores'},
        HAMMER_TIME = {'Hora del martillo', 'Usa un potenciador'},
        SMASHY_SMASHY = {'Hecho pedazos', 'Usa %s potenciadores'},
    },
    pt = {
        title = 'Estatísticas', progress = 'Progresso', stars = 'Estrelas', skins = 'Roupas dos pássaros',
        items = 'Itens de coleção', achievements = 'Conquistas', levels = 'Níveis concluídos',
        score = 'Pontuação total', birds = 'Pássaros lançados', pigs = 'Porcos estourados',
        earned = 'Moedas ganhas', spent = 'Moedas gastas', player_level = 'Nível do jogador',
        unlocked = 'Desbloqueada',
        STAR_GAZER = {'Observador de estrelas', 'Ganhe %s estrelas'},
        STAR_REACHER = {'Pega-estrelas', 'Ganhe %s estrelas'},
        STAR_TREKER = {'Viajante estelar', 'Ganhe %s estrelas'},
        STAR_BOUNDER = {'Salto estelar', 'Ganhe %s estrelas'},
        COIN_GRABBER = {'Pega-moedas', 'Ganhe %s moedas'},
        HEAVY_PURSE = {'Bolsa cheia', 'Ganhe %s moedas'},
        BIG_SPENDER = {'Gastador', 'Gaste %s moedas'},
        SNAPSHOT = {'Instantâneo', 'Desbloqueie %s roupa'},
        HEADSHOT = {'Retrato', 'Desbloqueie %s roupas'},
        CAMERA_ROLL = {'Rolo de filme', 'Desbloqueie %s roupas'},
        PAPARAZZI = {'Paparazzi', 'Desbloqueie %s roupas'},
        ALBUM_MAKER = {'Álbum de fotos', 'Desbloqueie %s roupas'},
        PIGGINING = {'Primeiros porcos', 'Estoure %s porcos'},
        PIG_OUT = {'Porcaria', 'Estoure %s porcos'},
        TREE_TOP_COMPLETE = {'Herói da copa', 'Conclua todos os níveis do capítulo 1'},
        TREE_TOP_HIGH_TOTAL_SCORE = {'Rei da copa', 'Faça %s pontos no capítulo 1'},
        BEACH_TOP_COMPLETE = {'Herói da praia', 'Conclua todos os níveis do capítulo 2'},
        BEACH_HIGH_TOTAL_SCORE = {'Rei da praia', 'Faça %s pontos no capítulo 2'},
        N_THREE_STARRED_LEVELS = {'Talento três estrelas', 'Consiga três estrelas em %s níveis'},
        N_THREE_STARRED_LEVELS_2 = {'Mestre três estrelas', 'Consiga três estrelas em %s níveis'},
        PARCOR_KID = {'Garota do parkour', 'Faça a Stella quicar %s vezes'},
        LEAPS_AND_BOUNDS = {'A passos largos', 'Faça a Stella quicar %s vezes'},
        HIGH_PLAY_TIME_1 = {'Aquecimento', 'Jogue por %s hora'},
        HIGH_PLAY_TIME_5 = {'Frequentador', 'Jogue por %s horas'},
        HIGH_PLAY_TIME_10 = {'Fã dedicado', 'Jogue por %s horas'},
        N_DRUM_BOUNCES_WITH_ONE_BIRD = {'Rufar de tambores', 'Faça um pássaro quicar %s vezes em tambores'},
        HAMMER_TIME = {'Hora do martelo', 'Use um power-up'},
        SMASHY_SMASHY = {'Em pedacinhos', 'Use %s power-ups'},
    },
    ru = {
        title = 'Статистика', progress = 'Прогресс', stars = 'Звезды', skins = 'Наряды птиц',
        items = 'Предметы коллекций', achievements = 'Достижения', levels = 'Пройдено уровней',
        score = 'Общий счет', birds = 'Запущено птиц', pigs = 'Раздавлено свиней', earned = 'Заработано монет',
        spent = 'Потрачено монет', player_level = 'Уровень игрока', unlocked = 'Открыто',
        STAR_GAZER = {'Звездочет', 'Заработай %s звезд'},
        STAR_REACHER = {'Ловец звезд', 'Заработай %s звезд'},
        STAR_TREKER = {'Звездный путь', 'Заработай %s звезд'},
        STAR_BOUNDER = {'Звездный прыжок', 'Заработай %s звезд'},
        COIN_GRABBER = {'Сборщик монет', 'Заработай %s монет'},
        HEAVY_PURSE = {'Тугой кошелек', 'Заработай %s монет'},
        BIG_SPENDER = {'Транжира', 'Потрать %s монет'},
        SNAPSHOT = {'Снимок', 'Открой %s наряд птицы'},
        HEADSHOT = {'Портрет', 'Открой %s нарядов птиц'},
        CAMERA_ROLL = {'Фотопленка', 'Открой %s нарядов птиц'},
        PAPARAZZI = {'Папарацци', 'Открой %s нарядов птиц'},
        ALBUM_MAKER = {'Фотоальбом', 'Открой %s нарядов птиц'},
        PIGGINING = {'Первые свиньи', 'Раздави %s свиней'},
        PIG_OUT = {'Свинство', 'Раздави %s свиней'},
        TREE_TOP_COMPLETE = {'Герой кроны', 'Пройди все уровни главы 1'},
        TREE_TOP_HIGH_TOTAL_SCORE = {'Король кроны', 'Набери %s очков в главе 1'},
        BEACH_TOP_COMPLETE = {'Герой пляжа', 'Пройди все уровни главы 2'},
        BEACH_HIGH_TOTAL_SCORE = {'Король пляжа', 'Набери %s очков в главе 2'},
        N_THREE_STARRED_LEVELS = {'Талант трех звезд', 'Получи три звезды на %s уровнях'},
        N_THREE_STARRED_LEVELS_2 = {'Мастер трех звезд', 'Получи три звезды на %s уровнях'},
        PARCOR_KID = {'Паркурщица', 'Заставь Стэллу отскочить %s раз'},
        LEAPS_AND_BOUNDS = {'Семимильные шаги', 'Заставь Стэллу отскочить %s раз'},
        HIGH_PLAY_TIME_1 = {'Разминка', 'Играй %s час'},
        HIGH_PLAY_TIME_5 = {'Завсегдатай', 'Играй %s часов'},
        HIGH_PLAY_TIME_10 = {'Преданный фанат', 'Играй %s часов'},
        N_DRUM_BOUNCES_WITH_ONE_BIRD = {'Барабанная дробь', 'Отбей одну птицу от барабанов %s раз'},
        HAMMER_TIME = {'Время молота', 'Используй усиление'},
        SMASHY_SMASHY = {'Вдребезги', 'Используй %s усилений'},
    },
    zh_TW = {
        title = '進度與成就', progress = '遊戲進度', stars = '星星', skins = '鳥兒服裝', items = '收藏品', achievements = '成就',
        levels = '已完成關卡', score = '總成績', birds = '彈射的鳥兒', pigs = '彈爆的豬豬', earned = '獲得的金幣', spent = '用掉的金幣',
        player_level = '玩家程度', unlocked = '已解鎖',
        STAR_GAZER = {'觀星者', '獲得 %s 顆星星'},
        STAR_REACHER = {'集星者', '獲得 %s 顆星星'},
        STAR_TREKER = {'星星旅人', '獲得 %s 顆星星'},
        STAR_BOUNDER = {'星星達人', '獲得 %s 顆星星'},
        COIN_GRABBER = {'金幣收集者', '獲得 %s 金幣'},
        HEAVY_PURSE = {'金幣滿滿', '獲得 %s 金幣'},
        BIG_SPENDER = {'出手大方', '用掉 %s 金幣'},
        SNAPSHOT = {'新裝上身', '解鎖 %s 套鳥兒服裝'},
        HEADSHOT = {'時髦鳥兒', '解鎖 %s 套鳥兒服裝'},
        CAMERA_ROLL = {'服裝收藏家', '解鎖 %s 套鳥兒服裝'},
        PAPARAZZI = {'鏡頭前', '解鎖 %s 套鳥兒服裝'},
        ALBUM_MAKER = {'服裝大師', '解鎖 %s 套鳥兒服裝'},
        PIGGINING = {'彈豬新手', '彈爆 %s 隻豬豬'},
        PIG_OUT = {'豬豬大掃除', '彈爆 %s 隻豬豬'},
        TREE_TOP_COMPLETE = {'第 1 區達人', '完成第 1 區的所有關卡'},
        TREE_TOP_HIGH_TOTAL_SCORE = {'第 1 區高手', '在第 1 區獲得 %s 分'},
        BEACH_TOP_COMPLETE = {'第 2 區達人', '完成第 2 區的所有關卡'},
        BEACH_HIGH_TOTAL_SCORE = {'第 2 區高手', '在第 2 區獲得 %s 分'},
        N_THREE_STARRED_LEVELS = {'3 星天才', '在 %s 個關卡獲得 3 顆星'},
        N_THREE_STARRED_LEVELS_2 = {'3 星大師', '在 %s 個關卡獲得 3 顆星'},
        PARCOR_KID = {'反彈小將', '讓 Stella  反彈 %s 次'},
        LEAPS_AND_BOUNDS = {'反彈大師', '讓 Stella  反彈 %s 次'},
        HIGH_PLAY_TIME_1 = {'起步', '遊玩 %s 小時'},
        HIGH_PLAY_TIME_5 = {'好朋友', '遊玩 %s 小時'},
        HIGH_PLAY_TIME_10 = {'最強粉絲', '遊玩 %s 小時'},
        N_DRUM_BOUNCES_WITH_ONE_BIRD = {'擊鼓', '讓一隻鳥從鼓上反彈 %s 次'},
        HAMMER_TIME = {'大錘時間', '使用一個強化道具'},
        SMASHY_SMASHY = {'大破壞', '使用 %s 個強化道具'},
    },
    ja = {
        title = 'データ', progress = 'ゲームの進行状況', stars = 'スター', skins = 'バードのコスチューム', items = 'コレクションアイテム',
        achievements = '実績', levels = 'クリアしたレベル', score = '総スコア', birds = 'とばしたバード', pigs = 'ポップしたピッグ',
        earned = '獲得したコイン', spent = '使ったコイン', player_level = 'あなたのレベル', unlocked = 'アンロック済み',
        STAR_GAZER = {'スターコレクター', 'スターを%sこ獲得する'},
        STAR_REACHER = {'スターハンター', 'スターを%sこ獲得する'},
        STAR_TREKER = {'スタートレッカー', 'スターを%sこ獲得する'},
        STAR_BOUNDER = {'スタージャンパー', 'スターを%sこ獲得する'},
        COIN_GRABBER = {'コインコレクター', 'コインを%s獲得する'},
        HEAVY_PURSE = {'コインもち', 'コインを%s獲得する'},
        BIG_SPENDER = {'お買い物上手', 'コインを%s使う'},
        SNAPSHOT = {'スナップショット', 'コスチュームを%sつアンロックする'},
        HEADSHOT = {'ポートレート', 'コスチュームを%sつアンロックする'},
        CAMERA_ROLL = {'フィルム', 'コスチュームを%sつアンロックする'},
        PAPARAZZI = {'パパラッチ', 'コスチュームを%sつアンロックする'},
        ALBUM_MAKER = {'アルバム', 'コスチュームを%sつアンロックする'},
        PIGGINING = {'ポップデビュー', 'ピッグを%s匹ポップする'},
        PIG_OUT = {'ポップマスター', 'ピッグを%s匹ポップする'},
        TREE_TOP_COMPLETE = {'ツリーのヒーロー', 'チャプター1のレベルをすべてクリアする'},
        TREE_TOP_HIGH_TOTAL_SCORE = {'ツリーのキング', 'チャプター1で%s点を獲得する'},
        BEACH_TOP_COMPLETE = {'ビーチのヒーロー', 'チャプター2のレベルをすべてクリアする'},
        BEACH_HIGH_TOTAL_SCORE = {'ビーチのキング', 'チャプター2で%s点を獲得する'},
        N_THREE_STARRED_LEVELS = {'3つ星ルーキー', '%sレベルで3つ星を獲得する'},
        N_THREE_STARRED_LEVELS_2 = {'3つ星マスター', '%sレベルで3つ星を獲得する'},
        PARCOR_KID = {'パルクールキッド', 'ステラを%sかいバウンドさせる'},
        LEAPS_AND_BOUNDS = {'バウンドマスター', 'ステラを%sかいバウンドさせる'},
        HIGH_PLAY_TIME_1 = {'スタート', '%s時間プレイする'},
        HIGH_PLAY_TIME_5 = {'レギュラー', '%s時間プレイする'},
        HIGH_PLAY_TIME_10 = {'大ファン', '%s時間プレイする'},
        N_DRUM_BOUNCES_WITH_ONE_BIRD = {'ドラムロール', '1羽のバードをドラムで%sかいバウンドさせる'},
        HAMMER_TIME = {'ハンマータイム', 'パワーアップを使う'},
        SMASHY_SMASHY = {'こなごな', 'パワーアップを%sかい使う'},
    },
    ko = {
        title = '통계', progress = '게임 진행도', stars = '별', skins = '새 의상', items = '수집품', achievements = '도전과제',
        levels = '완료한 레벨', score = '총점', birds = '날린 새', pigs = '터트린 돼지', earned = '획득한 코인', spent = '사용한 코인',
        player_level = '플레이어 레벨', unlocked = '완료',
        STAR_GAZER = {'별바라기', '별 %s개 획득'},
        STAR_REACHER = {'별 수집가', '별 %s개 획득'},
        STAR_TREKER = {'별 여행자', '별 %s개 획득'},
        STAR_BOUNDER = {'별 마스터', '별 %s개 획득'},
        COIN_GRABBER = {'코인 수집가', '코인 %s개 획득'},
        HEAVY_PURSE = {'가득한 지갑', '코인 %s개 획득'},
        BIG_SPENDER = {'큰 부자', '코인 %s개 사용'},
        SNAPSHOT = {'스냅샷', '새 의상 %s개 잠금 해제'},
        HEADSHOT = {'인물 사진', '새 의상 %s개 잠금 해제'},
        CAMERA_ROLL = {'필름', '새 의상 %s개 잠금 해제'},
        PAPARAZZI = {'파파라치', '새 의상 %s개 잠금 해제'},
        ALBUM_MAKER = {'사진 수집가', '새 의상 %s개 잠금 해제'},
        PIGGINING = {'돼지 터트리기 입문', '돼지 %s마리 터트리기'},
        PIG_OUT = {'돼지 대청소', '돼지 %s마리 터트리기'},
        TREE_TOP_COMPLETE = {'나무 위의 스타', '1장의 모든 레벨 완료'},
        TREE_TOP_HIGH_TOTAL_SCORE = {'나무 위의 최고', '1장에서 %s점 획득'},
        BEACH_TOP_COMPLETE = {'해변의 스타', '2장의 모든 레벨 완료'},
        BEACH_HIGH_TOTAL_SCORE = {'해변의 최고', '2장에서 %s점 획득'},
        N_THREE_STARRED_LEVELS = {'별 3개 달인', '레벨 %s개에서 별 3개 획득'},
        N_THREE_STARRED_LEVELS_2 = {'별 3개 마스터', '레벨 %s개에서 별 3개 획득'},
        PARCOR_KID = {'바운스 소녀', '스텔라로 %s번 바운스하기'},
        LEAPS_AND_BOUNDS = {'바운스 마스터', '스텔라로 %s번 바운스하기'},
        HIGH_PLAY_TIME_1 = {'준비 운동', '%s시간 플레이'},
        HIGH_PLAY_TIME_5 = {'베테랑', '%s시간 플레이'},
        HIGH_PLAY_TIME_10 = {'진정한 마니아', '%s시간 플레이'},
        N_DRUM_BOUNCES_WITH_ONE_BIRD = {'드럼 연주', '새 한 마리로 드럼에서 %s번 바운스하기'},
        HAMMER_TIME = {'망치 시간', '파워업 1개 사용'},
        SMASHY_SMASHY = {'파워 폭발', '파워업 %s개 사용'},
    },
}
)";

// The achievements, the progress, the page and the banner (Achievements.lua).
constexpr const char* kLogic = R"(
local function language() return res.getLocale() end
local text = statistics.texts(TEXTS, language)
local function number(n) return statistics.number(n, language()) end
local function game_text(key) return res.getString('TEXTS_BASIC', key) end

-- The achievements in the game's order, with an icon each; the social ones
-- (FRIENDS_SCORE_BEATEN, TOP_TIER, A_LEAGUE_OF_YOUR_OWN) need Facebook friends.
local LIST = {
    {'STAR_GAZER', 'MEDIUM_STAR'}, {'STAR_REACHER', 'MEDIUM_STAR'}, {'STAR_TREKER', 'MEDIUM_STAR'},
    {'STAR_BOUNDER', 'MEDIUM_STAR'}, {'COIN_GRABBER', 'SAVE_FILE_COIN'}, {'HEAVY_PURSE', 'SAVE_FILE_COIN'},
    {'BIG_SPENDER', 'SAVE_FILE_COIN'}, {'SNAPSHOT', 'STAR_REWARD_STELLA1'}, {'HEADSHOT', 'STAR_REWARD_POPPY1'},
    {'CAMERA_ROLL', 'STAR_REWARD_LUCA1'}, {'PAPARAZZI', 'STAR_REWARD_WILLOW1'},
    {'ALBUM_MAKER', 'STAR_REWARD_DAHLIA1', gifts = true}, {'PIGGINING', 'SAVE_FILE_PIG'},
    {'PIG_OUT', 'SAVE_FILE_PIG'}, {'TREE_TOP_COMPLETE', 'ICON_MAP', chapter = 'Chapter01'},
    {'TREE_TOP_HIGH_TOTAL_SCORE', 'ICON_TROPHY', chapter = 'Chapter01', score = 4300000},
    {'BEACH_TOP_COMPLETE', 'ICON_MAP', chapter = 'Chapter02'},
    {'BEACH_HIGH_TOTAL_SCORE', 'ICON_TROPHY', chapter = 'Chapter02', score = 5500000},
    {'N_THREE_STARRED_LEVELS', 'SCORE_GOLD_STAR'}, {'N_THREE_STARRED_LEVELS_2', 'SCORE_GOLD_STAR'},
    {'PARCOR_KID', 'EXTRABIRD_BUBBLE_STELLA_1'}, {'LEAPS_AND_BOUNDS', 'EXTRABIRD_BUBBLE_STELLA_1'},
    {'HIGH_PLAY_TIME_1', 'COIN_BIRDTIMER'}, {'HIGH_PLAY_TIME_5', 'COIN_BIRDTIMER'},
    {'HIGH_PLAY_TIME_10', 'COIN_BIRDTIMER'}, {'N_DRUM_BOUNCES_WITH_ONE_BIRD', 'EXTRABIRD_BUBBLE_POPPY_1', drums = 5},
    {'HAMMER_TIME', 'ICON_POWERUP_HAMMER'}, {'SMASHY_SMASHY', 'ICON_POWERUP_HAMMER'},
}
local BY_ID = {}
for _, a in ipairs(LIST) do a.id, a.icon = a[1], a[2]; BY_ID[a.id] = a end

-- The gift boxes' outfits open only with offline-shop (the list came from the
-- asset service).
local function gifts()
    return offlineShopGiftBoxes ~= nil or (Assets ~= nil and Assets.getAssetFilename('unlocked_gift_bird_skins.dat') ~= nil)
end
local function listed()
    local list = {}
    for _, a in ipairs(LIST) do
        if AchievementDefinitions[a.id] and (not a.gifts or gifts()) then list[#list + 1] = a end
    end
    return list
end
local function unlocked(a) return SettingsWrapper:hasAchievement(a.id) == true end

local function chapter_levels(chapter) return actions.myLevels[chapter] or {} end
local function playtime() return (SettingsWrapper:getPlaytime() or 0) + (playtimeCounter or 0) end

-- text, value, target
local function describe(a)
    local def = AchievementDefinitions[a.id] or {}
    local names = text(a.id) or {a.id, ''}
    local description = names[2]
    if def.counter then
        local target = def.unlockThreshold or 0
        return description:format(number(target)), SettingsWrapper:getAchievementCounter(def.counter) or 0, target
    elseif def.playTimeTreshold then
        local hours = math.floor(def.playTimeTreshold / 3600)
        return description:format(number(hours)), math.floor(playtime() / 3600), hours
    elseif a.score then
        local total = 0
        for _, level in ipairs(chapter_levels(a.chapter)) do
            total = total + ((highscores[level] or {}).score or 0)
        end
        return description:format(number(a.score)), total, a.score
    elseif a.chapter then
        local done, levels = 0, chapter_levels(a.chapter)
        for _, level in ipairs(levels) do
            if (SettingsWrapper:getTimesLevelCompleted(level) or 0) > 0 then done = done + 1 end
        end
        return description, done, #levels
    elseif a.drums then
        return description:format(number(a.drums))
    end
    return description
end
local function name(a) return (text(a.id) or {a.id})[1] end

-- Recomp Achievements (common/recomp_achievements.hpp): the chapters completed
-- lead to Beaten, the last one is its win; Album Maker needs offline-shop's gift
-- box outfits and is a bonus one.
local POINTS = {
    STAR_GAZER = 5, STAR_TREKER = 25, STAR_BOUNDER = 50, COIN_GRABBER = 5, BIG_SPENDER = 25, SNAPSHOT = 5,
    PAPARAZZI = 25, ALBUM_MAKER = 25, PIG_OUT = 25, TREE_TOP_HIGH_TOTAL_SCORE = 25, BEACH_TOP_COMPLETE = 25,
    BEACH_HIGH_TOTAL_SCORE = 25, N_THREE_STARRED_LEVELS_2 = 25, PARCOR_KID = 5, HIGH_PLAY_TIME_1 = 5,
    HIGH_PLAY_TIME_10 = 25, HAMMER_TIME = 5,
}
local STORY = {TREE_TOP_COMPLETE = 'progression', BEACH_TOP_COMPLETE = 'win'}
local recomp = {}
for _, a in ipairs(LIST) do
    recomp[#recomp + 1] = {key = a.id:lower(), type = STORY[a.id], points = POINTS[a.id] or 10, bonus = a.gifts,
        unlocked = function() return unlocked(a) end,
        describe = function() return name(a), (describe(a)) end}
end
_G.slingshotAchievements = recomp

-- The parts of the progress: stars, bird outfits, collection items, achievements.
local CHAPTERS = {'Chapter01', 'Chapter02'}
local function outfits()
    local gift = {}
    if not gifts() then
        for button, skin in pairs(GiftBirdSkins or {}) do
            if button:find('GIFT_BOX') then gift[skin] = true end
        end
    end
    local total, count = 0, 0
    for skin in pairs(g_telepodConfiguration or {}) do
        if not gift[skin] then total = total + 1 end
    end
    for _ in pairs(SettingsWrapper:getUnlockedBirdSkins() or {}) do count = count + 1 end
    return math.min(count, total), total
end
local function collection_items()
    local found, total = 0, 0
    for _, set in pairs(Collections or {}) do
        if type(set) == 'table' then
            for _, item in ipairs(set.upgrades or {}) do
                total = total + 1
                if PlayerState:getCollectionItemStatus(item.sprite) ~= 'LOCKED' then found = found + 1 end
            end
        end
    end
    return found, total
end
local function parts()
    local stars, max = 0, 0
    for _, chapter in ipairs(CHAPTERS) do
        for _, level in ipairs(chapter_levels(chapter)) do
            stars, max = stars + (getLevelStarsFromHighscores(level) or 0), max + 3
        end
    end
    local list = {{text('stars'), stars, max}}
    local count, total = outfits()
    list[#list + 1] = {text('skins'), count, total}
    count, total = collection_items()
    list[#list + 1] = {text('items'), count, total}
    local achievements = listed()
    count = 0
    for _, a in ipairs(achievements) do if unlocked(a) then count = count + 1 end end
    list[#list + 1] = {text('achievements'), count, #achievements}
    return list
end
local function counters()
    local done, total, score = 0, 0, 0
    for _, chapter in ipairs(CHAPTERS) do
        for _, level in ipairs(chapter_levels(chapter)) do
            total = total + 1
            if (SettingsWrapper:getTimesLevelCompleted(level) or 0) > 0 then done = done + 1 end
            score = score + ((highscores[level] or {}).score or 0)
        end
    end
    local minutes = math.floor(playtime() / 60)
    local time = game_text('TEXT_TIMER_HOURS'):format(math.floor(minutes / 60)) .. ' '
                 .. game_text('TEXT_TIMER_MINUTES'):format(minutes % 60)
    local W = SettingsWrapper
    return {
        {text('levels'), number(done) .. ' / ' .. number(total)},
        {text('score'), number(score)},
        {game_text('TEXT_MERGE_LAST_PLAYED'):gsub(':$', ''), time},
        {text('birds'), number(W:getBirdsShot() or 0)},
        {text('pigs'), number(W:getAchievementCounter('poppedPigs') or 0)},
        {text('earned'), number(W:getAchievementCounter('earnedCoins') or 0)},
        {text('spent'), number(W:getAchievementCounter('spentCoins') or 0)},
        {text('player_level'), number(PlayerState:getLevel() or 1)},
    }
end

-- UI helpers, in screen pixels.
local serial = 0
local function unique(base)
    serial = serial + 1
    return base .. serial
end
local FONT = 'FONT_STELLA_DARK'
local function label(parent, s, x, y, anchor, scale, max_width)
    local t = ui.Text:new({name = unique('label'), font = FONT, text = tostring(s), useLocalizedFont = true,
                           hanchor = anchor or 'LEFT', vanchor = 'TOP'})
    local w = (t:getWidth() or 0) * scale
    if max_width and w > max_width then scale = scale * max_width / w end
    t:setScale(scale)
    t.x, t.y = x, y
    t.scaledWidth = (t:getWidth() or 0) * scale
    parent:addChild(t)
    return t
end
local function rect(parent, x, y, w, h, r, g, b, a)
    local box = ui.Rectangle:new({name = unique('rect')})
    box.width, box.height = w, h
    box.r, box.g, box.b, box.a = r, g, b, a or 1
    box.x, box.y = x, y
    parent:addChild(box)
    return box
end
local function image(parent, sprite, x, y, sx, sy)
    local i = ui.Image:new({name = unique('image')})
    i:setImage(sprite)
    i.x, i.y = x, y
    i.scaleX, i.scaleY = sx, sy or sx
    parent:addChild(i)
    return i
end
local function button(parent, value, sprite, x, y, scale)
    local b = ui.ImageButton:new({name = unique('button')})
    b:setImage(sprite)
    b.x, b.y = x, y
    b.scaleX, b.scaleY = scale, scale
    b.returnValue = value
    parent:addChild(b)
    return b
end

local PER_PAGE = 6
local Page

local function make_page_class()
    Page = ui.Frame:inherit()

    function Page:init()
        ui.Frame.init(self, {name = 'gameStatisticsPage'})
        self:build(1)
    end

    -- The page for the screen size: designed for 1180 x 720 (the board and a
    -- margin), scaled to fit and centred; built again when the size changes.
    function Page:build(page)
        self:removeAllChildren()
        self.rows = nil
        local W, H = screenWidth, screenHeight
        self.builtW, self.builtH = W, H
        local s = math.min(H / 720, W / 1180)
        local cx, top = W / 2, (H - 720 * s) / 2
        self.s, self.top = s, top
        local function Y(v) return top + v * s end
        self.x, self.y = 0, 0
        rect(self, 0, 0, W, H, 0, 0, 0, 0.6)
        image(self, 'GENERAL_UI_BG', cx, Y(351), 1.64 * s, 1.142 * s)
        image(self, 'POPUP_BG_SMALL1', cx - 282 * s, Y(452), 1.48 * s, 2.17 * s)
        image(self, 'POPUP_BG_SMALL1', cx + 282 * s, Y(452), 1.48 * s, 2.17 * s)
        label(self, text('title'), cx, Y(166), 'HCENTER', 2 * s, 700 * s)
        button(self, 'STATISTICS_CLOSE', 'UI_BUTTON_WOOD_SMALL', cx + 475 * s, Y(155), s)

        -- left: the progress and the counters
        local left, right = cx - 495 * s, cx - 55 * s
        local list = parts()
        local total = statistics.progress(list)
        local y = Y(232)
        local percent = label(self, string.format('%d %%', math.floor(total * 100)), right, y, 'RIGHT', 1.5 * s)
        label(self, text('progress'), left, y, 'LEFT', 1.5 * s, right - left - percent.scaledWidth - 12 * s)
        y = y + 38 * s
        rect(self, left, y, right - left, 20 * s, 0.35, 0.2, 0.1, 0.5)
        rect(self, left + 3 * s, y + 3 * s, math.max((right - left - 6 * s) * total, 0), 14 * s, 1, 0.8, 0.1)
        y = y + 34 * s
        local row = 27 * s
        for _, p in ipairs(list) do
            local v = label(self, number(p[2]) .. ' / ' .. number(p[3]), right, y, 'RIGHT', 1.3 * s)
            label(self, p[1], left, y, 'LEFT', 1.3 * s, right - left - v.scaledWidth - 10 * s)
            y = y + row
        end
        y = y + 12 * s
        for _, c in ipairs(counters()) do
            local v = label(self, c[2], right, y, 'RIGHT', 1.3 * s)
            label(self, c[1], left, y, 'LEFT', 1.3 * s, right - left - v.scaledWidth - 10 * s)
            y = y + row
        end

        -- right: the achievements, a page at a time
        self.list = listed()
        local count = 0
        for _, a in ipairs(self.list) do if unlocked(a) then count = count + 1 end end
        self.area = {x0 = cx + 55 * s, x1 = cx + 495 * s, y = Y(232)}
        local n = label(self, number(count) .. ' / ' .. number(#self.list), self.area.x1, self.area.y, 'RIGHT', 1.5 * s)
        label(self, text('achievements'), self.area.x0, self.area.y, 'LEFT', 1.5 * s,
              self.area.x1 - self.area.x0 - n.scaledWidth - 12 * s)
        self.pages = math.max(1, math.ceil(#self.list / PER_PAGE))
        local by = Y(646)
        self.back = button(self, 'STATISTICS_BACK', 'BTN_BG_SMALL', self.area.x0 + 26 * s, by, 0.55 * s)
        image(self.back, 'ICON_ARROW_LEFT', 0, 0, 1)
        self.forward = button(self, 'STATISTICS_FORWARD', 'BTN_BG_SMALL', self.area.x1 - 26 * s, by, 0.55 * s)
        image(self.forward, 'ICON_ARROW_LEFT', 0, 0, -1, 1)
        self.pageText = label(self, '', (self.area.x0 + self.area.x1) / 2, by - 11 * s, 'HCENTER', 1.3 * s)
        self:showPage(page)
    end

    function Page:update(...)
        if screenWidth ~= self.builtW or screenHeight ~= self.builtH then self:build(self.page) end
        return ui.Frame.update(self, ...)
    end

    function Page:showPage(page)
        page = math.max(1, math.min(page, self.pages))
        self.page = page
        if self.rows then self:removeChild(self.rows) end
        local rows = ui.Frame:new({name = unique('rows')})
        self.rows = rows
        self:addChild(rows)
        local s = self.s
        local area = self.area
        local y = area.y + 40 * s
        local text_x = area.x0 + 56 * s
        for i = (page - 1) * PER_PAGE + 1, math.min(page * PER_PAGE, #self.list) do
            local a = self.list[i]
            local done = unlocked(a)
            local info, value, target = describe(a)
            local icon = image(rows, a.icon, area.x0 + 22 * s, y + 22 * s, 0.6 * s)
            local bw = icon.w and icon.w > 0 and icon.w or 64
            icon.scaleX, icon.scaleY = 44 * s / bw, 44 * s / bw
            if not done then icon.alpha = 0.35 end
            local status
            if done then
                status = text('unlocked')
            elseif value and target then
                status = number(math.min(value, target)) .. ' / ' .. number(target)
            end
            local status_w = 0
            if status then status_w = label(rows, status, area.x1, y + 3 * s, 'RIGHT', 1.1 * s).scaledWidth + 10 * s end
            label(rows, name(a), text_x, y, 'LEFT', 1.35 * s, area.x1 - text_x - status_w)
            label(rows, info or '', text_x, y + 28 * s, 'LEFT', 1.1 * s, area.x1 - text_x)
            y = y + 58 * s
        end
        self.pageText:setText(page .. ' / ' .. self.pages)
        self.back:setVisible(page > 1)
        self.forward:setVisible(page < self.pages)
    end

    function Page:close()
        if self.parent then self.parent:removeChild(self) end
    end

    function Page:onPointerEvent(...)
        local result = ui.Frame.onPointerEvent(self, ...)
        if result == 'STATISTICS_CLOSE' then
            self:close()
        elseif result == 'STATISTICS_BACK' then
            self:showPage(self.page - 1)
        elseif result == 'STATISTICS_FORWARD' then
            self:showPage(self.page + 1)
        end
        return 'BLOCK'
    end

    function Page:onKeyEvent(event, key)
        if event == 'PRESS' and (key == 'KEY_BACK' or key == 'ESCAPE') then self:close() end
        return 'BLOCK'
    end
end

function gameStatisticsOpen()
    if not Page then make_page_class() end
    eventManager:notify({id = events.EID_PUSH_FRAME, target = Page:new()})
end

-- Unlocks: a banner at the top, one at a time, above menus and levels.
local queue, banner = {}, nil
local BANNER_TIME = 3
local function show_next()
    local a = table.remove(queue, 1)
    if not a or not notificationsFrame then return end
    local W, H = screenWidth, screenHeight
    local s = H / 720
    banner = ui.Frame:new({name = unique('gameStatisticsBanner')})
    banner.x, banner.y = W / 2, 64 * s
    image(banner, 'UI_BG_POPUP', 0, 0, 1.3 * s, 0.6 * s)
    local icon = image(banner, a.icon, -160 * s, 0, 1)
    local bw = icon.w and icon.w > 0 and icon.w or 64
    icon.scaleX, icon.scaleY = 52 * s / bw, 52 * s / bw
    label(banner, game_text('TEXT_ACHIEVEMENT_UNLOCKED'), -122 * s, -28 * s, 'LEFT', 1.05 * s, 300 * s)
    label(banner, name(a), -122 * s, 2 * s, 'LEFT', 1.3 * s, 300 * s)
    banner.time = BANNER_TIME
    function banner:update(dt, ...)
        ui.Frame.update(self, dt, ...)
        self.time = self.time - (dt or 0)
        if self.time <= 0 then
            if self.parent then self.parent:removeChild(self) end
            banner = nil
            show_next()
        end
    end
    function banner:onPointerEvent() end
    notificationsFrame:addChild(banner)
end
local unlock = Achievements.unlock
function Achievements.unlock(id, ...)
    local new = not SettingsWrapper:hasAchievement(id)
    local results = {unlock(id, ...)}
    local a = BY_ID[id]
    if new and a and (not a.gifts or gifts()) then
        queue[#queue + 1] = a
        if not banner then show_next() end
    end
    return unpack(results)
end
)";

// The island: the trophy button right of the scrapbook, shown with the other
// buttons. Placed after every layout too, which puts it back where the layout
// file has it (bottom centre).
constexpr const char* kIsland = R"(
local function place_trophy(map)
    local b = map:getChild('btnAchievements')
    local scrapbook, settings = map:getChild('btnscrapbook'), map:getChild('btnsettings')
    if b and scrapbook and settings then
        b:setVisible(settings.visible)
        b.x = scrapbook.x + 1.3 * (b.w or 104) * (b.scaleX or 1)
        b.y = scrapbook.y
    end
end
local update, layout = ui.IslandMap.update, ui.IslandMap.layout
function ui.IslandMap:update(...)
    local results = {update(self, ...)}
    place_trophy(self)
    return unpack(results)
end
function ui.IslandMap:layout(...)
    local results = {layout(self, ...)}
    place_trophy(self)
    return unpack(results)
end
function ui.IslandMap:openAchievements() gameStatisticsOpen() end
)";

class GameStatistics final : public slingshot::Mod {
public:
    GameStatistics() {
        using slingshot::mods::append_lua;
        append_lua("data/scripts/gamerservices/Achievements.lua",
                   std::string(slingshot::mods::kStatisticsLua) + kTexts + kLogic);
        append_lua("data/scripts/menus/island/IslandMap.lua", kIsland);
        slingshot::mods::report_achievements();
    }
};

}  // namespace

SLINGSHOT_MOD(GameStatistics, "game-statistics",
              "Statistics, progress and achievements on a page of the island; unlocks show in a banner");
