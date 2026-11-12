// SPDX-License-Identifier: GPL-3.0-or-later
// composite-audio for Star Wars II (composite_audio.hpp).
#include "composite_audio/composite_audio.hpp"
#include "common/game_scripts.hpp"
#include "mod.hpp"
#include "recomp/game.hpp"

namespace {

// Looping is chosen when a sound is played (playAudio's third argument).
constexpr const char* kParts = R"(
local function playable(name)
    return name
end
local function play_once(name)
    play(name, nil, false, nil, true)  -- past the level's audio filters, as the composite
end
)";

class CompositeAudio final : public slingshot::Mod {
public:
    CompositeAudio() { slingshot::mods::append_lua(recomp::game.main_script, composite_audio::lua(kParts)); }
};

}  // namespace

SLINGSHOT_MOD(CompositeAudio, "composite-audio", "Plays the main menu music and the power-up sounds");
