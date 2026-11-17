// SPDX-License-Identifier: GPL-3.0-or-later
// Between sdl3_on_sdl2.cpp (SDL3 types) and sdl2_bridge.cpp (SDL2 and libnx):
// the two SDL versions cannot share a translation unit, so these use plain types.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace sdl2_bridge {

enum class Kind {
    None, Quit, Background, Foreground,
    WindowPixelSize, WindowMinimized, WindowRestored, WindowFocusLost, WindowMouseLeave,
    MouseMotion, MouseDown, MouseUp, MouseWheel,
    FingerDown, FingerMotion, FingerUp,
    KeyDown, KeyUp,
    PadAdded, PadRemoved, PadDown, PadUp, PadAxis,
};

struct Event {
    Kind kind = Kind::None;
    uint64_t timestamp_ns = 0;
    uint32_t window = 0;
    int64_t which = 0;      // mouse, joystick instance or touch device
    int64_t finger = 0;
    float x = 0, y = 0;     // mouse: window coordinates; finger: 0..1
    float dx = 0, dy = 0, pressure = 0;
    int data1 = 0, data2 = 0;  // window size; mouse button and clicks; pad button or axis and value
    bool flipped = false;
    uint32_t scancode = 0, key = 0;
    uint16_t mod = 0;
    bool repeat = false;
};

// SDL3's flags and attribute numbers are converted by the caller.
bool init(uint32_t flags);
bool init_subsystem(uint32_t flags);
uint32_t was_init(uint32_t flags);
void quit();
const char* error();
bool set_hint(const char* name, const char* value);
bool poll(Event* e);  // nullptr: only whether one is pending
bool wait();
void delay(uint32_t ms);

void* create_window(const char* title, int w, int h, uint32_t flags);
void destroy_window(void* window);
uint32_t window_flags(void* window);
uint32_t window_id(void* window);
bool set_fullscreen(void* window, bool on);
void window_size(void* window, int* w, int* h);
void drawable_size(void* window, int* w, int* h);
uint32_t mouse_state(float* x, float* y);
uint32_t key_from_scancode(uint32_t scancode);

bool gl_set_attribute(int sdl2_attr, int value);
void* gl_create_context(void* window);
void gl_delete_context(void* context);
void* gl_proc(const char* name);
bool gl_extension(const char* name);
bool gl_swap_interval(int interval);
void gl_swap(void* window);
void gl_unload();

void* open_pad(int64_t instance);  // a game controller by joystick instance id
void close_pad(void* pad);
bool pad_button(void* pad, int button);

// Pulls `len` bytes through `fill` on the audio thread.
using AudioFill = void (*)(void* user, uint8_t* out, int len);
uint32_t open_audio(int freq, uint16_t sdl2_format, int channels, int frames, AudioFill fill, void* user);
void close_audio(uint32_t device);
void pause_audio(uint32_t device, bool pause);
void audio_spec(uint32_t device, int* freq, int* frames);
const char* audio_driver();

std::string system_locale();  // "ll_CC" or "ll"

}  // namespace sdl2_bridge
