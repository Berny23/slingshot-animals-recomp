// SPDX-License-Identifier: GPL-3.0-or-later
// AudioOutput's sound device: an SDL audio stream that pulls PCM from the
// game on SDL's audio thread.
#pragma once

#include <SDL3/SDL.h>

#include <vector>

#include "recomp/java.hpp"

namespace slingshot {

class SdlAudio final : public recomp::java::AudioBackend {
public:
    ~SdlAudio() override { stop(); }
    bool start(const recomp::java::AudioFormat& format, size_t buffer_bytes, Fill fill) override;
    void stop() override;

private:
    static void SDLCALL callback(void* self, SDL_AudioStream* stream, int additional, int total);

    SDL_AudioStream* stream_ = nullptr;
    Fill fill_;
    std::vector<uint8_t> scratch_;
};

}  // namespace slingshot
