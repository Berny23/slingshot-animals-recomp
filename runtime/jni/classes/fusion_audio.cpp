// SPDX-License-Identifier: GPL-3.0-or-later
// com.rovio.fusion.AudioOutput: the engine's sound output, an android.media.AudioTrack
// in streaming mode that pulls PCM from the engine's mixer.
//
// The original creates the track with a buffer of max(requested, getMinBufferSize())
// bytes, primes it with one full nativeMixData() call and then, on every
// position notification (an eighth of the buffer), mixes and writes one
// period. Here the platform's audio thread asks for data; whole periods are
// mixed into a FIFO the same way and handed out from there.
//
// Mixing happens when the device needs data, so after the primed buffer has
// played the FIFO holds less than a period: the latency is the host's, not
// the ~370 ms of a full 64 KiB AudioTrack.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "recomp/java.hpp"
#include "recomp/jni.hpp"
#include "recomp/memory.hpp"
#include "recomp/runtime.hpp"

namespace recomp::java {
namespace {

using namespace recomp::jni;

constexpr const char* kAudio = "com/rovio/fusion/AudioOutput";

AudioBackend* g_backend = nullptr;

struct Output {
    int64_t native = 0;  // the engine's object (AudioOutput.e)
    AudioFormat format;
    uint32_t frame_bytes = 0;   // AudioOutput.c
    uint32_t buffer_bytes = 0;  // AudioOutput.k
    uint32_t period_bytes = 0;  // getPositionNotificationPeriod() * c
    bool playing = false;

    Ref self = 0, array = 0;  // global references while playing
    std::vector<uint8_t> fifo;
    size_t fifo_pos = 0;
    bool primed = false;

    // RECOMP_TRACE=audio: how much of real time the mixer needs, and when sounds
    // start after silence (for latency measurements).
    double mix_seconds = 0;
    uint64_t mixed_bytes = 0;
    double silent_ms = 0;
};

// AudioTrack.getMinBufferSize: what Android reports is about 40 ms of audio.
uint32_t min_buffer_size(const AudioFormat& f) {
    return uint32_t(f.rate / 25) * uint32_t(f.channels * f.bits / 8);
}

// Runs AudioOutput.nativeMixData(e, f, len) and appends the result to the FIFO.
void mix(Output& o, uint32_t len) {
    static const uint32_t fn = native_method(kAudio, "nativeMixData");
    thread_local CPU* cpu = nullptr;  // the audio thread's guest CPU
    if (!cpu) {
        cpu = create_cpu(512u << 10);
        bind_cpu(cpu);
    }
    const uint32_t data = array_data(o.array);
    std::memset(g_mem + data, o.format.bits == 8 ? 0x80 : 0, len);
    static const bool trace = trace_enabled("audio");
    const auto t0 = std::chrono::steady_clock::now();
    if (fn) {
        NativeFrame frame;
        const uint64_t n = uint64_t(o.native);
        // (JNIEnv*, this, jlong in r2:r3, byte[] and length on the stack)
        guest_call(cpu, fn, {env(), o.self, uint32_t(n), uint32_t(n >> 32), o.array, len});
    }
    if (trace) {
        o.mix_seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const uint64_t second = uint64_t(o.format.rate) * o.frame_bytes;
        if ((o.mixed_bytes + len) / (5 * second) != o.mixed_bytes / (5 * second)) {
            const double queued_ms = 1000.0 * double(o.fifo.size() - o.fifo_pos) / double(second);
            std::fprintf(stderr, "audio: mixer load %.1f%% of real time, %.0f ms queued\n", 100.0 * o.mix_seconds / 5.0,
                         queued_ms);
            o.mix_seconds = 0;
        }
        o.mixed_bytes += len;
        if (o.format.bits == 16) {
            const auto* pcm = reinterpret_cast<const int16_t*>(g_mem + data);
            size_t first = len / 2;
            for (size_t i = 0; i < len / 2; ++i)
                if (pcm[i] > 1000 || pcm[i] < -1000) {
                    first = i;
                    break;
                }
            if (first < len / 2 && o.silent_ms >= 150) {
                const double queued_ms = 1000.0 * double(o.fifo.size() - o.fifo_pos) / double(second);
                const double offset_ms = 1000.0 * double(first * 2) / double(second);
                std::fprintf(stderr, "audio: sound starts, mixed at %.1f ms, %.0f ms ahead in the queue\n",
                             std::chrono::duration<double, std::milli>(t0.time_since_epoch()).count(),
                             queued_ms + offset_ms);
            }
            o.silent_ms = first < len / 2 ? 0 : o.silent_ms + 1000.0 * len / double(second);
        }
    }
    o.fifo.insert(o.fifo.end(), g_mem + data, g_mem + data + len);
}

void fill(Output& o, uint8_t* dst, size_t len) {
    if (!o.primed) {  // startOutput(): one whole buffer first
        mix(o, o.buffer_bytes);
        o.primed = true;
    }
    while (o.fifo.size() - o.fifo_pos < len) mix(o, o.period_bytes);
    std::memcpy(dst, o.fifo.data() + o.fifo_pos, len);
    o.fifo_pos += len;
    if (o.fifo_pos > (1u << 16)) {
        o.fifo.erase(o.fifo.begin(), o.fifo.begin() + ptrdiff_t(o.fifo_pos));
        o.fifo_pos = 0;
    }
}

std::shared_ptr<Output> output(Ref self) { return std::static_pointer_cast<Output>(host_data(self)); }

// AudioOutput(long nativePtr, int sampleRate, int channels, int bitsPerSample, int bufferSize)
RECOMP_JAVA_METHOD(kAudio, "<init>", "(JIIII)V", [](Call& c) {
    auto o = std::make_shared<Output>();
    o->native = c.j(0);
    o->format.rate = c.i(1);
    o->format.channels = c.i(2) == 1 ? 1 : 2;  // CHANNEL_OUT_MONO or CHANNEL_OUT_STEREO
    o->format.bits = c.i(3) == 8 ? 8 : 16;      // ENCODING_PCM_8BIT or ENCODING_PCM_16BIT
    o->buffer_bytes = std::max(uint32_t(c.i(4)), min_buffer_size(o->format));
    o->frame_bytes = uint32_t(c.i(3) / 8 * c.i(2));
    if (!o->frame_bytes) o->frame_bytes = uint32_t(o->format.channels * o->format.bits / 8);
    const uint32_t frames = o->buffer_bytes / o->frame_bytes;
    o->period_bytes = std::min(frames / 8 * o->frame_bytes, o->buffer_bytes);
    if (!o->period_bytes) o->period_bytes = o->frame_bytes;
    set_host_data(c.self, o);
    if (trace_enabled("audio"))
        std::fprintf(stderr, "audio: AudioOutput %d Hz, %d ch, %d bit, buffer %u, period %u bytes\n", o->format.rate,
                     o->format.channels, o->format.bits, o->buffer_bytes, o->period_bytes);
    return Value(0);
});

RECOMP_JAVA_METHOD(kAudio, "startOutput", "()Z", [](Call& c) {
    auto o = output(c.self);
    if (!o) return boolean(false);
    if (o->playing) return boolean(true);
    if (!g_backend) return boolean(false);  // no audio device: the engine keeps asking
    o->self = new_global(c.self);
    o->array = new_global(new_array('B', o->buffer_bytes));
    o->fifo.clear();
    o->fifo_pos = 0;
    o->primed = false;
    Output* raw = o.get();
    if (!g_backend->start(o->format, o->buffer_bytes, [raw](uint8_t* dst, size_t len) { fill(*raw, dst, len); })) {
        delete_global(o->array);
        delete_global(o->self);
        o->self = o->array = 0;
        return boolean(false);
    }
    o->playing = true;
    return boolean(true);
});

RECOMP_JAVA_METHOD(kAudio, "stopOutput", "()V", [](Call& c) {
    auto o = output(c.self);
    if (!o || !o->playing) return Value(0);
    g_backend->stop();
    o->playing = false;
    delete_global(o->array);
    delete_global(o->self);
    o->self = o->array = 0;
    return Value(0);
});

// Audio focus (AudioManager.requestAudioFocus): nothing to do on the host.
RECOMP_JAVA_METHOD(kAudio, "requestExclusiveAudio", "(Z)V", [](Call&) { return Value(0); });

}  // namespace

void set_audio_backend(AudioBackend* backend) { g_backend = backend; }

}  // namespace recomp::java
