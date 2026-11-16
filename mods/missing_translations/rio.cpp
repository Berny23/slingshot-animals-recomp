// SPDX-License-Identifier: GPL-3.0-or-later
// Texts the game shows only in English, in its other languages. Rio's
// "Episode Locked" popup (EpisodeSelectionPage) is written into the script in
// English instead of coming from TEXTS_BASIC.
#include "common/game_scripts.hpp"
#include "mod.hpp"

namespace {

// Components.make_message_popup(_, title, text, ...) builds the popup.
constexpr const char* kComponents = R"(
local EPISODE_LOCKED = {
    de = {'Episode gesperrt', 'Schließe die vorherige Episode ab, um sie freizuschalten.'},
    fr = {'Épisode verrouillé', "Terminez l'épisode précédent pour le débloquer."},
    it = {'Episodio bloccato', "Completa l'episodio precedente per sbloccarlo."},
    es = {'Episodio bloqueado', 'Completa el episodio anterior para desbloquearlo.'},
    pt = {'Episódio bloqueado', 'Conclua o episódio anterior para desbloqueá-lo.'},
}
local message_popup = make_message_popup
function make_message_popup(first, title, text, ...)
    if title == 'Episode Locked' and text == 'Complete Previous episode to unlock' then
        local texts = EPISODE_LOCKED[tostring(_G.res.getLocale()):sub(1, 2)]
        if texts then title, text = texts[1], texts[2] end
    end
    return message_popup(first, title, text, ...)
end
)";

class MissingTranslations final : public slingshot::Mod {
public:
    MissingTranslations() { slingshot::mods::append_lua("data/scripts/Components.lua", kComponents); }
};

}  // namespace

SLINGSHOT_MOD(MissingTranslations, "missing-translations", "Translates texts the game shows only in English");
