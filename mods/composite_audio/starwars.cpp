// SPDX-License-Identifier: GPL-3.0-or-later
// composite-audio for Star Wars (composite_audio.hpp).
#include "composite_audio/composite_audio.hpp"
#include "common/game_scripts.hpp"
#include "mod.hpp"
#include "recomp/game.hpp"

namespace {

// Looping is set when a sound is created (the music's flag): a looping part plays as a
// copy created without it.
constexpr const char* kParts = R"(
local once = {}  -- part -> the name it plays as
local function playable(name)
    if once[name] then return once[name] end
    local sounds = audioData.sounds
    local sound = sounds[name]
    once[name] = name
    if sound and sound.flag then  -- as soundManager.lua's createAudio
        local copy = name .. '#once'
        G.ResourceManager.native_createAudio(sound.path, copy, false)
        sounds[copy] = {path = sound.path, name = copy, channel = sound.channel, volume = sound.volume, flag = false}
        once[name] = copy
    end
    return once[name]
end
local function play_once(name)
    play(playable(name), nil, nil, nil, true)  -- past the level's audio filters, as the composite
end
)";

class CompositeAudio final : public slingshot::Mod {
public:
    CompositeAudio() { slingshot::mods::append_lua(recomp::game.main_script, composite_audio::lua(kParts)); }
};

}  // namespace

SLINGSHOT_MOD(CompositeAudio, "composite-audio", "Plays the main menu music and the power-up sounds");
