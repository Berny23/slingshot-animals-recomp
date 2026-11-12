// SPDX-License-Identifier: GPL-3.0-or-later
#include "sdl_audio.hpp"

#include <cstdio>

#include "recomp/runtime.hpp"

namespace slingshot {

bool SdlAudio::start(const recomp::java::AudioFormat& format, size_t, Fill fill) {
    stop();
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "host: no audio: %s\n", SDL_GetError());
        return false;
    }
    // A small device buffer: data is mixed on demand, so this is most of the latency the
    // host adds. (A large request also raises the quantum of a whole PipeWire graph.)
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "512");
    const SDL_AudioSpec spec{format.bits == 8 ? SDL_AUDIO_U8 : SDL_AUDIO_S16LE, format.channels, format.rate};
    fill_ = std::move(fill);
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, callback, this);
    if (!stream_) {
        std::fprintf(stderr, "host: cannot open audio device: %s\n", SDL_GetError());
        fill_ = nullptr;
        return false;
    }
    SDL_ResumeAudioStreamDevice(stream_);
    if (recomp::trace_enabled("audio"))
        std::fprintf(stderr, "audio: stream started at %.1f ms\n", SDL_GetTicksNS() / 1e6);
    return true;
}

void SdlAudio::stop() {
    if (!stream_) return;
    SDL_DestroyAudioStream(stream_);  // waits for a running callback
    stream_ = nullptr;
    fill_ = nullptr;
}

void SDLCALL SdlAudio::callback(void* self, SDL_AudioStream* stream, int additional, int total) {
    auto* a = static_cast<SdlAudio*>(self);
    if (additional <= 0 || !a->fill_) return;
    a->scratch_.resize(size_t(additional));
    a->fill_(a->scratch_.data(), a->scratch_.size());
    SDL_PutAudioStreamData(stream, a->scratch_.data(), additional);
    static const bool trace = recomp::trace_enabled("audio");
    static int calls = 0;
    if (trace && ++calls % 200 == 1) {
        SDL_AudioSpec spec;
        int frames = 0;
        SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream), &spec, &frames);
        std::fprintf(stderr, "audio: device %s, %d Hz, %d frames per period; callback wants %d of %d bytes\n",
                     SDL_GetCurrentAudioDriver(), spec.freq, frames, additional, total);
    }
}

}  // namespace slingshot
