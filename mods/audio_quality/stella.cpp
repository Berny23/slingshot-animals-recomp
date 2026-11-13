// SPDX-License-Identifier: GPL-3.0-or-later
// audio-quality for Stella: the engine mixes at what the scripts ask of
// res.createAudioOutput(channels, bits, rate), on Android 16 kHz mono. Its sound
// files go up to 24 kHz, some in stereo; this mixes at that. gamelogic.lua sets up
// the output while it runs, through the scripts it loads, so this goes into the
// first of them.
#include "common/game_scripts.hpp"
#include "mod.hpp"

namespace {

constexpr const char* kLua = R"(
local res = res
local create = res.createAudioOutput
res.createAudioOutput = function(channels, bits, rate)
    return create(2, 16, 24000)
end
)";

class AudioQuality final : public slingshot::Mod {
public:
    AudioQuality() { slingshot::mods::append_lua("data/scripts_common/lua_util.lua", kLua); }
};

}  // namespace

SLINGSHOT_MOD(AudioQuality, "audio-quality", "Mixes sound in stereo at 24 kHz, the best of the game's files");
