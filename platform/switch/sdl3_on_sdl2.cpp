// SPDX-License-Identifier: GPL-3.0-or-later
// The SDL3 functions renamed in include/SDL3/SDL.h, on SDL2 (sdl2_bridge.cpp).
// Only what the host and the mods use.
#include <SDL3/SDL.h>
#include <sys/stat.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>

#include "sdl2_bridge.hpp"

namespace b = sdl2_bridge;

struct SDL_AudioStream {
    uint32_t device = 0;
    SDL_AudioStreamCallback callback = nullptr;
    void* user = nullptr;
    uint8_t silence = 0;
    uint8_t* out = nullptr;  // the device buffer while the callback runs
    int len = 0, filled = 0;
};

namespace {

int g_audio_frames = 0;  // SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES
std::string g_error;

void* window_of(SDL_Window* w) { return w; }

void convert(const b::Event& s, SDL_Event& e) {
    std::memset(&e, 0, sizeof e);
    e.common.timestamp = s.timestamp_ns;
    const SDL_MouseID mouse = s.which < 0 ? SDL_TOUCH_MOUSEID : SDL_MouseID(s.which);
    switch (s.kind) {
        case b::Kind::Quit: e.type = SDL_EVENT_QUIT; break;
        case b::Kind::Background: e.type = SDL_EVENT_WILL_ENTER_BACKGROUND; break;
        case b::Kind::Foreground: e.type = SDL_EVENT_DID_ENTER_FOREGROUND; break;
        case b::Kind::WindowPixelSize:
        case b::Kind::WindowMinimized:
        case b::Kind::WindowRestored:
        case b::Kind::WindowFocusLost:
        case b::Kind::WindowMouseLeave:
            e.type = s.kind == b::Kind::WindowPixelSize   ? SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED
                     : s.kind == b::Kind::WindowMinimized ? SDL_EVENT_WINDOW_MINIMIZED
                     : s.kind == b::Kind::WindowRestored  ? SDL_EVENT_WINDOW_RESTORED
                     : s.kind == b::Kind::WindowFocusLost ? SDL_EVENT_WINDOW_FOCUS_LOST
                                                          : SDL_EVENT_WINDOW_MOUSE_LEAVE;
            e.window.windowID = s.window;
            e.window.data1 = s.data1;
            e.window.data2 = s.data2;
            break;
        case b::Kind::MouseMotion:
            e.type = SDL_EVENT_MOUSE_MOTION;
            e.motion.windowID = s.window;
            e.motion.which = mouse;
            e.motion.x = s.x;
            e.motion.y = s.y;
            e.motion.xrel = s.dx;
            e.motion.yrel = s.dy;
            break;
        case b::Kind::MouseDown:
        case b::Kind::MouseUp:
            e.type = s.kind == b::Kind::MouseDown ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            e.button.windowID = s.window;
            e.button.which = mouse;
            e.button.button = Uint8(s.data1);
            e.button.clicks = Uint8(s.data2);
            e.button.down = s.kind == b::Kind::MouseDown;
            e.button.x = s.x;
            e.button.y = s.y;
            break;
        case b::Kind::MouseWheel:
            e.type = SDL_EVENT_MOUSE_WHEEL;
            e.wheel.windowID = s.window;
            e.wheel.which = mouse;
            e.wheel.x = s.dx;
            e.wheel.y = s.dy;
            e.wheel.direction = s.flipped ? SDL_MOUSEWHEEL_FLIPPED : SDL_MOUSEWHEEL_NORMAL;
            e.wheel.mouse_x = s.x;
            e.wheel.mouse_y = s.y;
            break;
        case b::Kind::FingerDown:
        case b::Kind::FingerMotion:
        case b::Kind::FingerUp:
            e.type = s.kind == b::Kind::FingerDown ? SDL_EVENT_FINGER_DOWN
                     : s.kind == b::Kind::FingerUp ? SDL_EVENT_FINGER_UP
                                                   : SDL_EVENT_FINGER_MOTION;
            e.tfinger.windowID = s.window;
            e.tfinger.touchID = SDL_TouchID(s.which);
            e.tfinger.fingerID = SDL_FingerID(s.finger);
            e.tfinger.x = s.x;
            e.tfinger.y = s.y;
            e.tfinger.dx = s.dx;
            e.tfinger.dy = s.dy;
            e.tfinger.pressure = s.pressure;
            break;
        case b::Kind::KeyDown:
        case b::Kind::KeyUp:
            // Scancodes, keycodes and modifier bits have the same values in both versions.
            e.type = s.kind == b::Kind::KeyDown ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            e.key.windowID = s.window;
            e.key.scancode = SDL_Scancode(s.scancode);
            e.key.key = SDL_Keycode(s.key);
            e.key.mod = SDL_Keymod(s.mod);
            e.key.down = s.kind == b::Kind::KeyDown;
            e.key.repeat = s.repeat;
            break;
        case b::Kind::PadAdded:
        case b::Kind::PadRemoved:
            e.type = s.kind == b::Kind::PadAdded ? SDL_EVENT_GAMEPAD_ADDED : SDL_EVENT_GAMEPAD_REMOVED;
            e.gdevice.which = SDL_JoystickID(s.which);
            break;
        case b::Kind::PadDown:
        case b::Kind::PadUp:
            // Button and axis numbers are the same in both versions.
            e.type = s.kind == b::Kind::PadDown ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
            e.gbutton.which = SDL_JoystickID(s.which);
            e.gbutton.button = Uint8(s.data1);
            e.gbutton.down = s.kind == b::Kind::PadDown;
            break;
        case b::Kind::PadAxis:
            e.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
            e.gaxis.which = SDL_JoystickID(s.which);
            e.gaxis.axis = Uint8(s.data1);
            e.gaxis.value = Sint16(s.data2);
            break;
        case b::Kind::None: break;
    }
}

// SDL3 attribute -> SDL2's number (SDL2 has SDL_GL_CONTEXT_EGL before the flags).
int sdl2_attr(SDL_GLAttr a) {
    switch (a) {
        case SDL_GL_CONTEXT_FLAGS: return 20;
        case SDL_GL_CONTEXT_PROFILE_MASK: return 21;
        case SDL_GL_SHARE_WITH_CURRENT_CONTEXT: return 22;
        case SDL_GL_FRAMEBUFFER_SRGB_CAPABLE: return 23;
        default: return a <= SDL_GL_CONTEXT_MINOR_VERSION ? int(a) : -1;
    }
}

void audio_fill(void* user, uint8_t* out, int len) {
    auto* s = static_cast<SDL_AudioStream*>(user);
    s->out = out;
    s->len = len;
    s->filled = 0;
    if (s->callback) s->callback(s->user, s, len, len);
    if (s->filled < len) std::memset(out + s->filled, s->silence, size_t(len - s->filled));
    s->out = nullptr;
}

}  // namespace

extern "C" {

bool sdl3c_Init(SDL_InitFlags flags) { return b::init(flags); }  // same flag bits in SDL2
bool sdl3c_InitSubSystem(SDL_InitFlags flags) { return b::init_subsystem(flags); }
SDL_InitFlags sdl3c_WasInit(SDL_InitFlags flags) { return b::was_init(flags); }
void sdl3c_Quit(void) { b::quit(); }
const char* sdl3c_GetError(void) { return g_error.empty() ? b::error() : g_error.c_str(); }
bool sdl3c_SetHint(const char* name, const char* value) {
    if (!std::strcmp(name, SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES)) {
        g_audio_frames = value ? std::atoi(value) : 0;
        return true;
    }
    return b::set_hint(name, value);
}
bool sdl3c_SetAppMetadata(const char*, const char*, const char*) { return true; }

bool sdl3c_PollEvent(SDL_Event* e) {
    if (!e) return b::poll(nullptr);
    b::Event s;
    if (!b::poll(&s)) return false;
    convert(s, *e);
    return true;
}
bool sdl3c_WaitEvent(SDL_Event* e) {
    if (e) return false;  // only the host's "wait for anything" is used
    return b::wait();
}
void sdl3c_Delay(Uint32 ms) { b::delay(ms); }
Uint64 sdl3c_GetTicksNS(void) {
    static const auto start = std::chrono::steady_clock::now();
    return Uint64(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
}
void sdl3c_free(void* p) { std::free(p); }

const char* sdl3c_GetBasePath(void) { return "romfs:/"; }
char* sdl3c_GetPrefPath(const char*, const char* app) {
    const std::string dir = std::string("sdmc:/switch/") + app + "/";
    mkdir("sdmc:/switch", 0777);
    mkdir(dir.c_str(), 0777);
    return strdup(dir.c_str());
}
SDL_Locale** sdl3c_GetPreferredLocales(int* count) {
    const std::string loc = b::system_locale();
    const size_t us = loc.find('_');
    const std::string lang = loc.substr(0, us), country = us == std::string::npos ? "" : loc.substr(us + 1);
    // One block, freed with SDL_free: the list, the locale, the strings.
    const size_t size = 2 * sizeof(SDL_Locale*) + sizeof(SDL_Locale) + lang.size() + country.size() + 2;
    auto* block = static_cast<char*>(std::calloc(1, size));
    auto** list = reinterpret_cast<SDL_Locale**>(block);
    auto* l = reinterpret_cast<SDL_Locale*>(block + 2 * sizeof(SDL_Locale*));
    char* text = block + 2 * sizeof(SDL_Locale*) + sizeof(SDL_Locale);
    std::memcpy(text, lang.c_str(), lang.size() + 1);
    l->language = text;
    if (!country.empty()) {
        std::memcpy(text + lang.size() + 1, country.c_str(), country.size() + 1);
        l->country = text + lang.size() + 1;
    }
    list[0] = l;
    if (count) *count = 1;
    return list;
}

SDL_Window* sdl3c_CreateWindow(const char* title, int w, int h, SDL_WindowFlags flags) {
    // The low flag bits (fullscreen, OpenGL, resizable, high pixel density) are SDL2's too.
    return static_cast<SDL_Window*>(b::create_window(title, w, h, Uint32(flags & 0xffffffff)));
}
void sdl3c_DestroyWindow(SDL_Window* w) { b::destroy_window(window_of(w)); }
SDL_WindowFlags sdl3c_GetWindowFlags(SDL_Window* w) { return b::window_flags(window_of(w)); }
SDL_WindowID sdl3c_GetWindowID(SDL_Window* w) { return b::window_id(window_of(w)); }
bool sdl3c_SetWindowFullscreen(SDL_Window* w, bool on) { return b::set_fullscreen(window_of(w), on); }
bool sdl3c_SetWindowIcon(SDL_Window*, SDL_Surface*) { return true; }
bool sdl3c_GetWindowSize(SDL_Window* w, int* x, int* y) {
    b::window_size(window_of(w), x, y);
    return true;
}
bool sdl3c_GetWindowSizeInPixels(SDL_Window* w, int* x, int* y) {
    b::drawable_size(window_of(w), x, y);
    return true;
}
float sdl3c_GetWindowPixelDensity(SDL_Window* w) {
    int ww = 0, wh = 0, pw = 0, ph = 0;
    b::window_size(window_of(w), &ww, &wh);
    b::drawable_size(window_of(w), &pw, &ph);
    return ww > 0 ? float(pw) / float(ww) : 1.0f;
}
float sdl3c_GetWindowDisplayScale(SDL_Window*) { return 1.0f; }
SDL_Surface* sdl3c_LoadPNG(const char*) {
    g_error = "no PNG loader";
    return nullptr;
}
void sdl3c_DestroySurface(SDL_Surface*) {}
SDL_MouseButtonFlags sdl3c_GetMouseState(float* x, float* y) { return b::mouse_state(x, y); }
SDL_Keycode sdl3c_GetKeyFromScancode(SDL_Scancode s, SDL_Keymod, bool) { return b::key_from_scancode(s); }

bool sdl3c_GL_SetAttribute(SDL_GLAttr attr, int value) {
    const int a = sdl2_attr(attr);
    return a >= 0 && b::gl_set_attribute(a, value);
}
SDL_GLContext sdl3c_GL_CreateContext(SDL_Window* w) { return static_cast<SDL_GLContext>(b::gl_create_context(window_of(w))); }
bool sdl3c_GL_DestroyContext(SDL_GLContext c) {
    b::gl_delete_context(c);
    return true;
}
SDL_FunctionPointer sdl3c_GL_GetProcAddress(const char* name) { return reinterpret_cast<SDL_FunctionPointer>(b::gl_proc(name)); }
bool sdl3c_GL_ExtensionSupported(const char* name) { return b::gl_extension(name); }
bool sdl3c_GL_SetSwapInterval(int interval) { return b::gl_swap_interval(interval); }
bool sdl3c_GL_SwapWindow(SDL_Window* w) {
    b::gl_swap(window_of(w));
    return true;
}
void sdl3c_GL_UnloadLibrary(void) { b::gl_unload(); }

SDL_Gamepad* sdl3c_OpenGamepad(SDL_JoystickID id) { return static_cast<SDL_Gamepad*>(b::open_pad(id)); }
void sdl3c_CloseGamepad(SDL_Gamepad* pad) {
    if (pad) b::close_pad(pad);
}
bool sdl3c_GetGamepadButton(SDL_Gamepad* pad, SDL_GamepadButton button) { return pad && b::pad_button(pad, button); }

SDL_AudioStream* sdl3c_OpenAudioDeviceStream(SDL_AudioDeviceID, const SDL_AudioSpec* spec,
                                             SDL_AudioStreamCallback callback, void* user) {
    auto* s = new SDL_AudioStream;
    s->callback = callback;
    s->user = user;
    s->silence = spec->format == SDL_AUDIO_U8 ? 0x80 : 0;
    // The format numbers (SDL_AUDIO_U8, SDL_AUDIO_S16LE) are SDL2's too.
    s->device = b::open_audio(spec->freq, uint16_t(spec->format), spec->channels, g_audio_frames, audio_fill, s);
    if (!s->device) {
        delete s;
        return nullptr;
    }
    return s;  // paused, as in SDL3
}
void sdl3c_DestroyAudioStream(SDL_AudioStream* s) {
    if (!s) return;
    b::close_audio(s->device);  // waits for a running callback
    delete s;
}
bool sdl3c_PutAudioStreamData(SDL_AudioStream* s, const void* data, int len) {
    if (!s->out) return false;
    const int n = len < s->len - s->filled ? len : s->len - s->filled;
    if (n > 0) std::memcpy(s->out + s->filled, data, size_t(n));
    s->filled += n > 0 ? n : 0;
    return true;
}
bool sdl3c_ResumeAudioStreamDevice(SDL_AudioStream* s) {
    b::pause_audio(s->device, false);
    return true;
}
SDL_AudioDeviceID sdl3c_GetAudioStreamDevice(SDL_AudioStream* s) { return s->device; }
bool sdl3c_GetAudioDeviceFormat(SDL_AudioDeviceID dev, SDL_AudioSpec* spec, int* frames) {
    int freq = 0, n = 0;
    b::audio_spec(dev, &freq, &n);
    if (spec) *spec = {SDL_AUDIO_S16LE, 2, freq};
    if (frames) *frames = n;
    return true;
}
const char* sdl3c_GetCurrentAudioDriver(void) { return b::audio_driver(); }

}  // extern "C"
