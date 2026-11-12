// SPDX-License-Identifier: GPL-3.0-or-later
// composite-audio: plays the sounds made of several parts (res.createCompositeAudio:
// the main menu music, the power-up activation sounds), which these versions of the
// engine accept but never play.
//
// A composite that does not start natively plays here part after part, each with its
// own file, volume and channel (soundManager.lua) and without looping, the next one
// starting on the frame after the previous one ended. The game's checks and stops
// see the composite, not its parts.
#pragma once

#include <string>

namespace composite_audio {

// Before the game's part: the originals.
inline constexpr const char* kHead = R"(
local G = _G
local res = G.res
local parts_of = {}  -- composite name -> its parts' names
local current         -- {name, parts, index, started}
local play, is_playing, stop, stop_all = res.playAudio, res.isAudioPlaying, res.stopAudio, res.stopAllAudio
)";

// After the game's part, which defines playable(part) (the name a part plays as,
// called when the composite is created) and play_once(part).
inline constexpr const char* kTail = R"(
local create = res.createCompositeAudio
res.createCompositeAudio = function(name, parts, ...)
    parts_of[name] = parts
    for _, part in G.ipairs(parts) do playable(part) end
    return create(name, parts, ...)
end

local function playing_part()
    return current and playable(current.parts[current.index])
end
local function play_part()
    play_once(current.parts[current.index])
    current.started = is_playing(playing_part())
end
local function wrapped_play(name, ...)
    local results = {play(name, ...)}
    local parts = parts_of[name]
    if parts and #parts > 0 and not is_playing(name) then
        if current then stop(playing_part()) end
        current = {name = name, parts = parts, index = 1}
        play_part()
    end
    return G.unpack(results)
end
-- As natively, a part played as part of a composite is not playing by its own name.
local function part(name)
    return playing_part() == name
end
local function wrapped_is_playing(name, ...)
    if current and current.name == name then return true end
    if part(name) then return false end
    return is_playing(name, ...)
end
local function wrapped_stop(name, ...)
    if part(name) then return end
    if current and current.name == name then
        stop(playing_part())
        current = nil
    end
    return stop(name, ...)
end
local function wrapped_stop_all(...)
    current = nil
    return stop_all(...)
end
res.playAudio, res.isAudioPlaying, res.stopAudio, res.stopAllAudio =
    wrapped_play, wrapped_is_playing, wrapped_stop, wrapped_stop_all
playAudio, stopAudio = wrapped_play, wrapped_stop  -- gamelogic.lua's globals

-- Per frame, from update and isSafeToQuit (the engine calls one of them on every frame).
local function advance()
    if not current then return end
    if not current.started then  -- a sound may start only on a later frame
        play_part()
    elseif not is_playing(playing_part()) then
        current.index = current.index + 1
        if current.index > #current.parts then current = nil else play_part() end
    end
end
for _, name in G.ipairs({'update', 'isSafeToQuit'}) do
    local original = G.getfenv(1)[name]
    if original then
        G.getfenv(1)[name] = function(...)
            advance()
            return original(...)
        end
    end
end
)";

inline std::string lua(const char* game_part) { return std::string(kHead) + game_part + kTail; }

}  // namespace composite_audio
