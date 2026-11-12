// SPDX-License-Identifier: GPL-3.0-or-later
// The emulated Java side of the game (runtime/jni/classes).
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "recomp/jni.hpp"

namespace recomp::java {

// The game's Activity (com.rovio.fusion.App), created on first use.
jni::Ref activity();
// Guest path of the app's private files directory (Context.getFilesDir()).
std::string files_dir();

// Display reported to the game (DeviceInfoWrapper); set by the platform layer.
struct Display {
    int width = 1280, height = 720;  // pixels
    float dpi = 160;
};
void set_display(const Display& d);
const Display& display();

// The device locale (java.util.Locale.getDefault()); set by the platform layer.
struct Locale {
    std::string language = "en";  // ISO 639, lower case
    std::string country = "US";   // ISO 3166, upper case; may be empty
};
void set_locale(const Locale& l);
const Locale& locale();
// "de_DE.UTF-8", "pt-BR", "ja": language[_-]COUNTRY with anything after '.' or '@' ignored.
// "C" and "POSIX" (and unparsable names) give en_US.
Locale parse_locale(const std::string& name);

// Work the original posts with Globals.runOnAppThread / runOnGLThread (callbacks into
// the game). The host runs it on the game thread, before the next frame's update.
void post_to_game_thread(std::function<void()> fn);
void run_posted();

// Sound output for AudioOutput (the game's android.media.AudioTrack); set by the platform layer.
struct AudioFormat {
    int rate = 44100;
    int channels = 2;
    int bits = 16;  // 16: signed little-endian, 8: unsigned
};
class AudioBackend {
public:
    // Fills dst with exactly len bytes of PCM; called on the backend's audio thread.
    using Fill = std::function<void(uint8_t* dst, size_t len)>;
    virtual ~AudioBackend() = default;
    // Starts playback; buffer_bytes is the AudioTrack buffer size.
    virtual bool start(const AudioFormat& format, size_t buffer_bytes, Fill fill) = 0;
    // Stops playback; no fill call runs after it returns.
    virtual void stop() = 0;
};
void set_audio_backend(AudioBackend* backend);

// System fonts for SystemFontRenderer (Typeface.create and the glyph fallback
// chain Android uses for characters the typeface lacks); set by the platform layer.
struct FontRequest {
    std::string family = "sans-serif";  // Android family: sans-serif, serif or monospace
    int weight = 400;                    // 400 regular, 700 bold, 300 light, ...
    bool italic = false;
    bool condensed = false;
};
struct FontFile {
    std::string path;
    int index = 0;  // face in a collection
    int weight = 400;
    bool italic = false;
};
class FontProvider {
public:
    virtual ~FontProvider() = default;
    // The font closest to the request. Bold/italic styles the file lacks are synthesized.
    virtual std::optional<FontFile> typeface(const FontRequest& request) = 0;
    // A font close to the request that has a glyph for cp.
    virtual std::optional<FontFile> fallback(const FontRequest& request, char32_t cp) = 0;
};
void set_font_provider(FontProvider* provider);

}  // namespace recomp::java
