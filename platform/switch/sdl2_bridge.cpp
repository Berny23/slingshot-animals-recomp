// SPDX-License-Identifier: GPL-3.0-or-later
#include "sdl2_bridge.hpp"

#include <SDL2/SDL.h>
#include <switch.h>

#include <cstring>

namespace sdl2_bridge {
namespace {

uint64_t ns(uint32_t ms) { return uint64_t(ms) * 1'000'000; }

// SDL2 reports gamepads by device index when they are added, SDL3 by instance id.
SDL_GameController* controller_of(int64_t instance) {
    for (int i = 0, n = SDL_NumJoysticks(); i < n; ++i)
        if (SDL_IsGameController(i) && SDL_JoystickGetDeviceInstanceID(i) == instance) return SDL_GameControllerOpen(i);
    return nullptr;
}

bool convert(const SDL_Event& s, Event& e) {
    e = {};
    e.timestamp_ns = ns(s.common.timestamp);
    switch (s.type) {
        case SDL_QUIT: e.kind = Kind::Quit; return true;
        case SDL_APP_WILLENTERBACKGROUND: e.kind = Kind::Background; return true;
        case SDL_APP_DIDENTERFOREGROUND: e.kind = Kind::Foreground; return true;
        case SDL_WINDOWEVENT:
            e.window = s.window.windowID;
            switch (s.window.event) {
                case SDL_WINDOWEVENT_SIZE_CHANGED: {
                    e.kind = Kind::WindowPixelSize;
                    SDL_Window* w = SDL_GetWindowFromID(s.window.windowID);
                    if (w) SDL_GL_GetDrawableSize(w, &e.data1, &e.data2);
                    return true;
                }
                case SDL_WINDOWEVENT_MINIMIZED: e.kind = Kind::WindowMinimized; return true;
                case SDL_WINDOWEVENT_RESTORED: e.kind = Kind::WindowRestored; return true;
                case SDL_WINDOWEVENT_FOCUS_LOST: e.kind = Kind::WindowFocusLost; return true;
                case SDL_WINDOWEVENT_LEAVE: e.kind = Kind::WindowMouseLeave; return true;
                default: return false;
            }
        case SDL_MOUSEMOTION:
            e.kind = Kind::MouseMotion;
            e.window = s.motion.windowID;
            e.which = s.motion.which == SDL_TOUCH_MOUSEID ? -1 : int64_t(s.motion.which);
            e.x = float(s.motion.x);
            e.y = float(s.motion.y);
            e.dx = float(s.motion.xrel);
            e.dy = float(s.motion.yrel);
            return true;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            e.kind = s.type == SDL_MOUSEBUTTONDOWN ? Kind::MouseDown : Kind::MouseUp;
            e.window = s.button.windowID;
            e.which = s.button.which == SDL_TOUCH_MOUSEID ? -1 : int64_t(s.button.which);
            e.x = float(s.button.x);
            e.y = float(s.button.y);
            e.data1 = s.button.button;
            e.data2 = s.button.clicks;
            return true;
        case SDL_MOUSEWHEEL:
            e.kind = Kind::MouseWheel;
            e.window = s.wheel.windowID;
            e.which = s.wheel.which == SDL_TOUCH_MOUSEID ? -1 : int64_t(s.wheel.which);
            e.dx = s.wheel.preciseX;
            e.dy = s.wheel.preciseY;
            e.x = float(s.wheel.mouseX);
            e.y = float(s.wheel.mouseY);
            e.flipped = s.wheel.direction == SDL_MOUSEWHEEL_FLIPPED;
            return true;
        case SDL_FINGERDOWN:
        case SDL_FINGERMOTION:
        case SDL_FINGERUP:
            e.kind = s.type == SDL_FINGERDOWN ? Kind::FingerDown : s.type == SDL_FINGERUP ? Kind::FingerUp : Kind::FingerMotion;
            e.window = s.tfinger.windowID;
            e.which = s.tfinger.touchId;
            e.finger = s.tfinger.fingerId;
            e.x = s.tfinger.x;
            e.y = s.tfinger.y;
            e.dx = s.tfinger.dx;
            e.dy = s.tfinger.dy;
            e.pressure = s.tfinger.pressure;
            return true;
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            e.kind = s.type == SDL_KEYDOWN ? Kind::KeyDown : Kind::KeyUp;
            e.window = s.key.windowID;
            e.scancode = s.key.keysym.scancode;
            e.key = uint32_t(s.key.keysym.sym);
            e.mod = s.key.keysym.mod;
            e.repeat = s.key.repeat;
            return true;
        case SDL_CONTROLLERDEVICEADDED:
            e.kind = Kind::PadAdded;
            e.which = SDL_JoystickGetDeviceInstanceID(s.cdevice.which);
            return true;
        case SDL_CONTROLLERDEVICEREMOVED:
            e.kind = Kind::PadRemoved;
            e.which = s.cdevice.which;  // already the instance id
            return true;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
            e.kind = s.type == SDL_CONTROLLERBUTTONDOWN ? Kind::PadDown : Kind::PadUp;
            e.which = s.cbutton.which;
            e.data1 = s.cbutton.button;  // same numbers as SDL3's (SOUTH = A ...)
            return true;
        case SDL_CONTROLLERAXISMOTION:
            e.kind = Kind::PadAxis;
            e.which = s.caxis.which;
            e.data1 = s.caxis.axis;
            e.data2 = s.caxis.value;
            return true;
        default:
            return false;
    }
}

}  // namespace

bool init(uint32_t flags) { return SDL_Init(flags) == 0; }
bool init_subsystem(uint32_t flags) { return SDL_InitSubSystem(flags) == 0; }
uint32_t was_init(uint32_t flags) { return SDL_WasInit(flags); }
void quit() { SDL_Quit(); }
const char* error() { return SDL_GetError(); }
bool set_hint(const char* name, const char* value) { return SDL_SetHint(name, value); }
void delay(uint32_t ms) { SDL_Delay(ms); }

bool poll(Event* e) {
    SDL_Event s;
    if (!e) return SDL_PollEvent(nullptr);
    while (SDL_PollEvent(&s))
        if (convert(s, *e)) return true;
    return false;
}

bool wait() { return SDL_WaitEvent(nullptr); }

void* create_window(const char* title, int w, int h, uint32_t flags) { return SDL_CreateWindow(title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h, flags); }
void destroy_window(void* window) { SDL_DestroyWindow(static_cast<SDL_Window*>(window)); }
uint32_t window_flags(void* window) { return SDL_GetWindowFlags(static_cast<SDL_Window*>(window)); }
uint32_t window_id(void* window) { return SDL_GetWindowID(static_cast<SDL_Window*>(window)); }
bool set_fullscreen(void* window, bool on) {
    return SDL_SetWindowFullscreen(static_cast<SDL_Window*>(window), on ? SDL_WINDOW_FULLSCREEN : 0) == 0;
}
void window_size(void* window, int* w, int* h) { SDL_GetWindowSize(static_cast<SDL_Window*>(window), w, h); }
void drawable_size(void* window, int* w, int* h) { SDL_GL_GetDrawableSize(static_cast<SDL_Window*>(window), w, h); }
uint32_t mouse_state(float* x, float* y) {
    int ix = 0, iy = 0;
    const uint32_t buttons = SDL_GetMouseState(&ix, &iy);
    if (x) *x = float(ix);
    if (y) *y = float(iy);
    return buttons;
}
uint32_t key_from_scancode(uint32_t scancode) { return uint32_t(SDL_GetKeyFromScancode(SDL_Scancode(scancode))); }

bool gl_set_attribute(int attr, int value) { return SDL_GL_SetAttribute(SDL_GLattr(attr), value) == 0; }
void* gl_create_context(void* window) { return SDL_GL_CreateContext(static_cast<SDL_Window*>(window)); }
void gl_delete_context(void* context) { SDL_GL_DeleteContext(context); }
void* gl_proc(const char* name) { return SDL_GL_GetProcAddress(name); }
bool gl_extension(const char* name) { return SDL_GL_ExtensionSupported(name); }
bool gl_swap_interval(int interval) { return SDL_GL_SetSwapInterval(interval) == 0; }
void gl_swap(void* window) { SDL_GL_SwapWindow(static_cast<SDL_Window*>(window)); }
void gl_unload() { SDL_GL_UnloadLibrary(); }

void* open_pad(int64_t instance) {
    if (SDL_GameController* c = SDL_GameControllerFromInstanceID(SDL_JoystickID(instance))) return c;
    return controller_of(instance);
}
void close_pad(void* pad) { SDL_GameControllerClose(static_cast<SDL_GameController*>(pad)); }
bool pad_button(void* pad, int button) {
    return SDL_GameControllerGetButton(static_cast<SDL_GameController*>(pad), SDL_GameControllerButton(button));
}

namespace {
struct AudioUser {
    AudioFill fill;
    void* user;
};
AudioUser g_audio_user{};
int g_audio_frames = 0, g_audio_freq = 0;
void SDLCALL audio_callback(void* user, Uint8* out, int len) {
    auto* a = static_cast<AudioUser*>(user);
    a->fill(a->user, out, len);
}
}  // namespace

uint32_t open_audio(int freq, uint16_t format, int channels, int frames, AudioFill fill, void* user) {
    g_audio_user = {fill, user};
    SDL_AudioSpec want{}, have{};
    want.freq = freq;
    want.format = format;
    want.channels = Uint8(channels);
    // SDL2's Switch driver has only two wave buffers of `samples` frames, and the game mixes in
    // the callback: with the desktop's 512 frames (12 ms at 44.1 kHz) any slow mix crackles.
    constexpr int kMinFrames = 2048;
    want.samples = Uint16(frames > kMinFrames ? frames : kMinFrames);
    want.callback = audio_callback;
    want.userdata = &g_audio_user;
    // SDL2 converts to the device's format, so the game's stays as asked.
    const SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (dev) {
        g_audio_frames = have.samples;
        g_audio_freq = have.freq;
    }
    return dev;
}
void close_audio(uint32_t device) { SDL_CloseAudioDevice(device); }
void pause_audio(uint32_t device, bool pause) { SDL_PauseAudioDevice(device, pause ? 1 : 0); }
void audio_spec(uint32_t, int* freq, int* frames) {
    *freq = g_audio_freq;
    *frames = g_audio_frames;
}
const char* audio_driver() { return SDL_GetCurrentAudioDriver(); }

std::string system_locale() {
    u64 code = 0;
    if (R_FAILED(setInitialize())) return "en_US";
    const Result r = setGetSystemLanguage(&code);
    setExit();
    if (R_FAILED(r)) return "en_US";
    char text[9] = {};
    std::memcpy(text, &code, 8);  // e.g. "en-US", "de", "zh-Hans"
    const std::string s(text);
    // The games need a country too (a bare "de" falls back to English).
    static const struct {
        const char *code, *locale;
    } kLocales[] = {{"ja", "ja_JP"},      {"en-US", "en_US"},   {"fr", "fr_FR"},     {"de", "de_DE"},
                    {"it", "it_IT"},      {"es", "es_ES"},      {"zh-CN", "zh_CN"},  {"ko", "ko_KR"},
                    {"nl", "nl_NL"},      {"pt", "pt_PT"},      {"ru", "ru_RU"},     {"zh-TW", "zh_TW"},
                    {"en-GB", "en_GB"},   {"fr-CA", "fr_CA"},   {"es-419", "es_MX"}, {"zh-Hans", "zh_CN"},
                    {"zh-Hant", "zh_TW"}, {"pt-BR", "pt_BR"}};
    for (const auto& l : kLocales)
        if (s == l.code) return l.locale;
    return "en_US";
}

}  // namespace sdl2_bridge
