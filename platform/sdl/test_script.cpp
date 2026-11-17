// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_script.hpp"

#include "lua_console.hpp"
#include "recomp/vfs.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>

namespace slingshot {
namespace {

constexpr SDL_TouchID kScriptTouch = 0x5c217;  // touch device of scripted fingers

}  // namespace

void save_screenshot(SDL_Window* window, const std::string& path) {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    using ReadPixels = void (*)(int, int, int, int, unsigned, unsigned, void*);
    const auto read = reinterpret_cast<ReadPixels>(SDL_GL_GetProcAddress("glReadPixels"));
    SDL_Surface* s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ABGR8888);
    if (!read || !s) return;
    read(0, 0, w, h, 0x1908 /* GL_RGBA */, 0x1401 /* GL_UNSIGNED_BYTE */, s->pixels);
    SDL_FlipSurface(s, SDL_FLIP_VERTICAL);
    if (SDL_SaveBMP(s, path.c_str())) std::fprintf(stderr, "host: saved %s\n", path.c_str());
    SDL_DestroySurface(s);
}

TestScript::TestScript(SDL_Window* window) : window_(window) {
#ifndef _WIN32
    if (const char* control = std::getenv("SLINGSHOT_CONTROL")) {
        if (mkfifo(control, 0600) && errno != EEXIST) {
            std::fprintf(stderr, "host: cannot create %s: %s\n", control, std::strerror(errno));
        } else {
            live_ = true;
            std::remove(recomp::vfs_map(kConsoleFile).c_str());  // left from an earlier run
            // Blocks in reads for the rest of the run; the process ends without joining it.
            std::thread(&TestScript::read_control, this, std::string(control)).detach();
            std::fprintf(stderr, "host: control pipe %s\n", control);
        }
    }
#endif
    const char* path = std::getenv("SLINGSHOT_SCRIPT");
    if (!path) return;
    std::ifstream in(path);
    if (!in) {
        std::fprintf(stderr, "host: cannot read script %s\n", path);
        return;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (const auto hash = line.find('#'); hash != std::string::npos) line.resize(hash);
        std::istringstream ss(line);
        Command c;
        if (!(ss >> c.frame)) continue;
        for (std::string w; ss >> w;) c.args.push_back(w);
        if (!c.args.empty()) commands_.push_back(std::move(c));
    }
    std::fprintf(stderr, "host: script %s, %zu commands\n", path, commands_.size());
}

void TestScript::read_control(std::string path) {
    for (;;) {
        std::ifstream in(path);  // waits for a writer; EOF when the last one closes
        for (std::string line; std::getline(in, line);) {
            Command c{0, {}, {}};
            std::istringstream ss(line);
            std::string w;
            if (!(ss >> w)) continue;
            if (w[0] == '+') {
                c.frame = std::strtol(w.c_str() + 1, nullptr, 10);
                if (!(ss >> w)) continue;
            }
            if (w == "lua") {  // the rest of the line as it is ('#' is Lua's length operator)
                std::getline(ss, c.text);
                c.args.push_back(w);
            } else if (w[0] != '#') {
                for (c.args.push_back(w); ss >> w && w[0] != '#';) c.args.push_back(w);
            } else {
                continue;
            }
            std::lock_guard lock(pending_mutex_);
            pending_.push_back(std::move(c));
        }
    }
}

TestScript::~TestScript() {
    if (pad_) SDL_CloseJoystick(pad_);
    if (pad_id_) SDL_DetachVirtualJoystick(pad_id_);
}

bool TestScript::run_input(long frame) {
    if (console_remove_frame_ && frame >= console_remove_frame_) {
        std::remove(recomp::vfs_map(kConsoleFile).c_str());
        console_remove_frame_ = 0;
    }
    if (live_) {
        std::lock_guard lock(pending_mutex_);
        // Commands that have run are dropped; new ones are scheduled from this frame on.
        std::erase_if(commands_, [frame](const Command& c) { return c.frame < frame; });
        for (Command& c : pending_) {
            c.frame += frame;
            commands_.push_back(std::move(c));
        }
        pending_.clear();
    }
    for (const Command& c : commands_) {
        if (c.frame != frame) continue;
        const auto& a = c.args;
        auto f = [&](size_t i) { return i < a.size() ? std::strtof(a[i].c_str(), nullptr) : 0.0f; };
        if (a[0] == "quit") return false;
        if (a[0] == "mark")  // steady_clock, as the audio trace uses
            std::fprintf(stderr, "host: mark %s at %.1f ms\n", a.size() > 1 ? a[1].c_str() : "",
                         std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count());
        if (a[0] == "lua" && !c.text.empty()) {
            // Written whole, then renamed: the game never reads half of it. A syntax
            // error makes the file itself fail to load, which the console reports.
            const std::string path = recomp::vfs_map(kConsoleFile);
            std::ofstream(path + ".tmp") << "return " << ++console_number_ << ", function()\n"
                                         << c.text << "\nend\n";
            std::rename((path + ".tmp").c_str(), path.c_str());
            console_remove_frame_ = frame + 60;  // the game runs it on its next frame
        }
        if (a[0] == "mouse" && a.size() >= 4) push_mouse(a[1], f(2), f(3));
        else if (a[0] == "wheel" && a.size() >= 4) {
            SDL_Event e{};
            e.type = SDL_EVENT_MOUSE_WHEEL;
            e.wheel.windowID = SDL_GetWindowID(window_);
            e.wheel.which = 1;
            e.wheel.y = f(3);
            e.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
            e.wheel.mouse_x = f(1);
            e.wheel.mouse_y = f(2);
            SDL_PushEvent(&e);
        } else if (a[0] == "finger" && a.size() >= 5) push_finger(a[1], std::atoi(a[2].c_str()), f(3), f(4));
        else if (a[0] == "key" && a.size() >= 3) push_key(a[1] == "down", a[2]);
        else if (a[0] == "pad") pad(a);
        else if (a[0] == "window" && a.size() >= 2) {  // what the window manager reports
            SDL_Event e{};
            e.type = a[1] == "minimize" ? SDL_EVENT_WINDOW_MINIMIZED : SDL_EVENT_WINDOW_RESTORED;
            e.window.windowID = SDL_GetWindowID(window_);
            SDL_PushEvent(&e);
        }
    }
    return true;
}

void TestScript::run_shots(long frame) {
    for (const Command& c : commands_) {
        if (c.frame != frame) continue;
        if (c.args[0] == "shot" && c.args.size() >= 2) save_screenshot(window_, c.args[1]);
        // At the end of the frame, after its screenshots.
        if (c.args[0] == "sync")
            std::fprintf(stderr, "host: sync %s frame %ld\n", c.args.size() > 1 ? c.args[1].c_str() : "", frame);
    }
}

void TestScript::push_mouse(const std::string& what, float x, float y) {
    SDL_Event e{};
    if (what == "move") {
        e.type = SDL_EVENT_MOUSE_MOTION;
        e.motion.windowID = SDL_GetWindowID(window_);
        e.motion.which = 1;
        e.motion.x = x;
        e.motion.y = y;
    } else {
        e.type = what == "down" ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.windowID = SDL_GetWindowID(window_);
        e.button.which = 1;
        e.button.button = SDL_BUTTON_LEFT;
        e.button.down = what == "down";
        e.button.clicks = 1;
        e.button.x = x;
        e.button.y = y;
    }
    SDL_PushEvent(&e);
}

void TestScript::push_finger(const std::string& what, int n, float x, float y) {
    int w = 1, h = 1;
    SDL_GetWindowSize(window_, &w, &h);
    SDL_Event e{};
    e.type = what == "down" ? SDL_EVENT_FINGER_DOWN : what == "up" ? SDL_EVENT_FINGER_UP : SDL_EVENT_FINGER_MOTION;
    e.tfinger.touchID = kScriptTouch;
    e.tfinger.fingerID = SDL_FingerID(n + 1);
    e.tfinger.x = x / w;
    e.tfinger.y = y / h;
    e.tfinger.pressure = what == "up" ? 0.0f : 1.0f;
    e.tfinger.windowID = SDL_GetWindowID(window_);
    SDL_PushEvent(&e);
}

void TestScript::push_key(bool down, const std::string& name) {
    const SDL_Scancode sc = SDL_GetScancodeFromName(name.c_str());
    if (sc == SDL_SCANCODE_UNKNOWN) {
        std::fprintf(stderr, "host: script: unknown key %s\n", name.c_str());
        return;
    }
    SDL_Event e{};
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.windowID = SDL_GetWindowID(window_);
    e.key.scancode = sc;
    e.key.key = SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, true);
    e.key.down = down;
    SDL_PushEvent(&e);
}

void TestScript::pad(const std::vector<std::string>& a) {
    if (a.size() >= 2 && a[1] == "attach") {
        if (pad_id_) return;
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.name = "Scripted gamepad";
        pad_id_ = SDL_AttachVirtualJoystick(&desc);
        pad_ = pad_id_ ? SDL_OpenJoystick(pad_id_) : nullptr;
        if (!pad_) std::fprintf(stderr, "host: script: virtual gamepad failed: %s\n", SDL_GetError());
        return;
    }
    if (!pad_ || a.size() < 3) return;
    if (a[1] == "down" || a[1] == "up") {
        const SDL_GamepadButton b = SDL_GetGamepadButtonFromString(a[2].c_str());
        if (b == SDL_GAMEPAD_BUTTON_INVALID) std::fprintf(stderr, "host: script: unknown button %s\n", a[2].c_str());
        else SDL_SetJoystickVirtualButton(pad_, b, a[1] == "down");
    } else if (a[1] == "axis" && a.size() >= 4) {
        const SDL_GamepadAxis ax = SDL_GetGamepadAxisFromString(a[2].c_str());
        float v = std::strtof(a[3].c_str(), nullptr);
        // Triggers are 0..1 on a gamepad but use the full raw range of the virtual axis.
        if (ax == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || ax == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) v = v * 2 - 1;
        if (ax == SDL_GAMEPAD_AXIS_INVALID) std::fprintf(stderr, "host: script: unknown axis %s\n", a[2].c_str());
        else SDL_SetJoystickVirtualAxis(pad_, ax, Sint16(v < 0 ? v * 32768 : v * 32767));
    }
}

}  // namespace slingshot
