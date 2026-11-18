// SPDX-License-Identifier: GPL-3.0-or-later
// keyboard for the games after Classic: the keys become the touches a player
// would make (Classic's classic.cpp sets the game's input globals instead).
//
//   Space (hold)          grab the bird; W A S D or arrows aim (Shift: slowly);
//                         release to shoot. A new grab starts at the last aim,
//                         and waits for the bird to stand still on the screen.
//   Space                 while a bird flies: its ability (a touch, held as long
//                         as Space is); W A S D or arrows move the touch, which
//                         aiming abilities follow (Shift: slowly)
//   arrow keys            move the camera (a drag on the scenery)
//   + / -                 zoom in / out (a pinch; also = and the keypad's + and -)
//   Z                     switch between the close and the far view, where the game has it
//   R                     restart the level (in play: pause, then the pause menu's restart)
//   P                     pause (the same as Esc, the Android back key)
//   Enter                 level end: next level, or retry
//
// The game's script part, KB below, reports where the bird and the buttons are
// (send); the host makes the touches. Z, which has no touch, goes to the script
// as a counter in a virtual file.
//
// A game's file defines its KB functions in Lua and appends
// keyboard_lua(adapter) to recomp::game.main_script:
//   KB.playing()        a level is played, nothing over it
//   KB.bird()           x, y, reach: the bird ready in the slingshot and the
//                       finger distance of a full stretch, screen pixels
//   KB.flying()         a bird flies and a tap triggers its ability
//   KB.button(name)     x, y of the shown 'restart' or 'next' button
//   KB.level()          the level's name
//   KB.zoom()           optional: Z
#pragma once

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <utility>

#include "common/game_scripts.hpp"
#include "common/pinch.hpp"
#include "common/script_link.hpp"
#include "mod.hpp"
#include "recomp/game.hpp"
#include "recomp/vfs.hpp"

namespace slingshot::mods::keyboard {

constexpr const char* kStatusPrefix = "cache/slingshot-keyboard/";
constexpr const char* kActionsFile = "cache/slingshot-keyboard.lua";

// Lua for menus built from `ui` Frames (children, visible, returnValue):
// find_button(frame, value) gives the screen position of the shown, enabled
// button returning `value`. Frames place themselves with getPositionOnScreen or
// transformToScreen, or else by x, y (the centre of a button) within their
// parent.
constexpr const char* kFrameButtonsLua = R"(
local function find_button(frame, value, left, top)
    left, top = left or 0, top or 0
    for _, child in _G.ipairs(frame and frame.children or {}) do
        if child.visible ~= false then
            local cx, cy = left + (child.x or 0), top + (child.y or 0)
            if child.returnValue == value and child.enabled ~= false then
                local at = child.getPositionOnScreen or child.transformToScreen
                local x, y = cx, cy
                if at then x, y = at(child, 0, 0) end
                if x and x >= 0 and y >= 0 and x < screenWidth and y < screenHeight then return x, y end
            end
            local x, y = find_button(child, value, cx, cy)
            if x then return x, y end
        end
    end
end
)";

inline std::string keyboard_lua(const std::string& adapter) {
    return std::string(kEachFrameLua) + "local KB = {}\n" + adapter + R"(
local STATUS = ')" + kStatusPrefix + R"('
local ACTIONS = ')" + kActionsFile + R"('
local seen, last = {}, nil
local function point(x, y) return G.string.format('%.1f,%.1f', x, y) end
each_frame(function()
    local actions = read(ACTIONS)
    for name, n in G.pairs(G.type(actions) == 'table' and actions or {}) do
        if n ~= (seen[name] or 0) and KB[name] then KB[name]() end
        seen[name] = n
    end
    local parts = {}
    if KB.playing() then
        parts[#parts + 1] = 'p=1'
        local x, y, reach = KB.bird()
        if x and (x < 0 or y < 0 or x >= screenWidth or y >= screenHeight) then x = nil end  -- the camera is elsewhere
        if x then parts[#parts + 1] = 'b=' .. point(x, y) .. G.string.format(',%.1f', reach) end
        if KB.flying() then parts[#parts + 1] = 'f=1' end
        parts[#parts + 1] = 'l=' .. G.tostring(KB.level() or ''):gsub('[^%w_]', ''):sub(1, 24)
    end
    for key, name in G.pairs({R = 'restart', N = 'next'}) do
        local x, y = KB.button(name)
        if x then parts[#parts + 1] = key .. '=' .. point(x, y) end
    end
    local status = G.table.concat(parts, ';')
    if status ~= last then
        last = status
        send(STATUS, status)
    end
end)
)";
}

// The keys, by the name the host uses.
enum Key { kSpace, kW, kA, kS, kD, kUp, kDown, kLeft, kRight, kShift, kPlus, kMinus, kZ, kR, kP, kEnter, kKeyCount };

inline int key_of(const SDL_KeyboardEvent& k) {
    // W A S D by position (any layout), the others by what the key shows.
    switch (k.scancode) {
        case SDL_SCANCODE_W: return kW;
        case SDL_SCANCODE_A: return kA;
        case SDL_SCANCODE_S: return kS;
        case SDL_SCANCODE_D: return kD;
        case SDL_SCANCODE_SPACE: return kSpace;
        case SDL_SCANCODE_UP: return kUp;
        case SDL_SCANCODE_DOWN: return kDown;
        case SDL_SCANCODE_LEFT: return kLeft;
        case SDL_SCANCODE_RIGHT: return kRight;
        case SDL_SCANCODE_LSHIFT:
        case SDL_SCANCODE_RSHIFT: return kShift;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER: return kEnter;
        case SDL_SCANCODE_KP_PLUS: return kPlus;
        case SDL_SCANCODE_KP_MINUS: return kMinus;
        default: break;
    }
    switch (k.key) {
        case SDLK_PLUS:
        case SDLK_EQUALS: return kPlus;
        case SDLK_MINUS: return kMinus;
        case SDLK_Z: return kZ;
        case SDLK_R: return kR;
        case SDLK_P: return kP;
        default: return -1;
    }
}

struct Point {
    bool set = false;
    float x = 0, y = 0;
};

struct Status {
    bool playing = false, flying = false;
    Point bird, restart, next;
    float reach = 0;
    std::string level;
};

inline Status parse_status(const std::string& text) {
    Status s;
    size_t i = 0;
    while (i < text.size()) {
        size_t end = text.find(';', i);
        if (end == std::string::npos) end = text.size();
        const std::string part = text.substr(i, end - i);
        i = end + 1;
        if (part.size() < 2 || part[1] != '=') continue;
        const char* v = part.c_str() + 2;
        float a = 0, b = 0, c = 0;
        switch (part[0]) {
            case 'p': s.playing = true; break;
            case 'f': s.flying = true; break;
            case 'l': s.level = v; break;
            case 'b':
                if (std::sscanf(v, "%f,%f,%f", &a, &b, &c) == 3) s.bird = {true, a, b}, s.reach = c;
                break;
            case 'R':
            case 'N':
                if (std::sscanf(v, "%f,%f", &a, &b) == 2) (part[0] == 'R' ? s.restart : s.next) = {true, a, b};
                break;
        }
    }
    return s;
}

class Keyboard : public slingshot::Mod {
public:
    explicit Keyboard(const std::string& adapter) {
        slingshot::mods::append_lua(recomp::game.main_script, keyboard_lua(adapter));
        on_script_message(kStatusPrefix, [this](const std::string& text) {
            std::lock_guard lk(mutex_);
            const Point bird = status_.bird;
            status_ = parse_status(text);
            if (status_.bird.x != bird.x || status_.bird.y != bird.y || !bird.set) bird_moved_ns_ = SDL_GetTicksNS();
        });
        recomp::vfs_add_virtual_file(kActionsFile, [this] {
            std::lock_guard lk(mutex_);
            return "return {zoom=" + std::to_string(zooms_) + "}";
        });
    }

    bool on_event(const SDL_Event& e, ModContext&) override {
        if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
            if (e.key.repeat) return false;
            const int k = key_of(e.key);
            if (k < 0) return false;
            if (e.type == SDL_EVENT_KEY_DOWN && !down_[k]) pressed_[k] = true;
            down_[k] = e.type == SDL_EVENT_KEY_DOWN;
        } else if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
            down_.fill(false);
        }
        return false;  // the keys still reach the game
    }

    void on_frame(ModContext& ctx) override {
        Status s;
        bool bird_still = false;
        {
            std::lock_guard lk(mutex_);
            s = status_;
            bird_still = SDL_GetTicksNS() - bird_moved_ns_ >= kBirdStillNs;
        }
        // The script reports game pixels; touches are surface pixels.
        const SurfaceRect& a = ctx.screen;
        for (Point* p : {&s.bird, &s.restart, &s.next}) p->x = a.x + p->x * ctx.screen_scale, p->y = a.y + p->y * ctx.screen_scale;
        s.reach *= ctx.screen_scale;
        const float dt = last_ns_ ? std::min((ctx.now_ns - last_ns_) * 1e-9f, 0.1f) : 0.0f;
        last_ns_ = ctx.now_ns;
        std::array<bool, kKeyCount> pressed = pressed_;
        pressed_.fill(false);

        if (tap_ >= 0) {  // the second frame of a tap
            ctx.input.pointer_up(tap_);
            tap_ = -1;
        }
        // P works at any time, as Esc does.
        if (back_ && !down_[kP]) {  // a press shorter than a frame still lasts one
            ctx.input.key(false, android::key::kBack, 0, android::kBuiltInKeyboard);
            back_ = false;
        } else if (pressed[kP] && !back_) {
            ctx.input.key(true, android::key::kBack, 0, android::kBuiltInKeyboard);
            back_ = true;
        }
        if (ability_ >= 0) {
            if (down_[kSpace]) return steer(dt, ctx);
            ctx.input.pointer_up(ability_);
            ability_ = -1;
        }
        if (aim_ >= 0) return aim(s, dt, ctx);

        if (s.playing) {
            if (pressed[kSpace]) {
                if (s.flying) {
                    ability_x_ = a.x + a.width * 0.5f;
                    ability_y_ = a.y + a.height * 0.5f;
                    tap(ability_x_, ability_y_, ctx);
                    std::swap(ability_, tap_);  // released with Space
                    return;
                }
                want_grab_ = true;
            }
            if (want_grab_ && down_[kSpace] && s.bird.set && bird_still) {
                want_grab_ = false;
                return grab(s, ctx);
            }
            if (!down_[kSpace]) want_grab_ = false;
            if (pressed[kZ]) {
                std::lock_guard lk(mutex_);
                ++zooms_;
            }
            pan(s, dt, ctx);
            const float zoom = float(down_[kPlus]) - float(down_[kMinus]);
            if (zoom != 0 && pan_ < 0) pinch_.zoom(std::exp(zoom * kZoomRate * dt), a.x + a.width * 0.5f, a.y + a.height * 0.5f, ctx.now_ns);
        } else {
            end_pan(ctx);
            want_grab_ = false;
        }
        pinch_.update(ctx, kPinchIdleNs);

        if (restart_ns_ && ctx.now_ns > restart_ns_) restart_ns_ = 0;  // no pause menu came
        if (restart_ns_ && s.restart.set) {
            // The pause menu slides in: tap once its button stopped moving.
            if (s.restart.x == restart_x_ && s.restart.y == restart_y_) {
                restart_ns_ = 0;
                return tap(s.restart.x, s.restart.y, ctx);
            }
            restart_x_ = s.restart.x;
            restart_y_ = s.restart.y;
        }
        if (pressed[kR]) {
            if (s.restart.set) {
                tap(s.restart.x, s.restart.y, ctx);
            } else if (s.playing && aim_ < 0) {
                ctx.input.key(true, android::key::kBack, 0, android::kBuiltInKeyboard);
                ctx.input.key(false, android::key::kBack, 0, android::kBuiltInKeyboard);
                restart_ns_ = ctx.now_ns + kRestartWaitNs;
                restart_x_ = restart_y_ = -1;
            }
        } else if (pressed[kEnter] && !s.playing) {
            if (s.next.set) tap(s.next.x, s.next.y, ctx);
            else if (s.restart.set) tap(s.restart.x, s.restart.y, ctx);
        }
    }

private:
    static constexpr float kAimSpeed = 0.8f, kFineAimSpeed = 0.1f;  // the band's reach per second
    static constexpr float kAimLimit = 1.2f;
    static constexpr float kPanSpeed = 0.8f;  // surface widths per second
    static constexpr float kSteerSpeed = 0.9f, kFineSteerSpeed = 0.2f;  // screen heights per second
    static constexpr float kZoomRate = 0.9f;  // natural log of the finger distance change per second
    static constexpr uint64_t kPinchIdleNs = 100'000'000;
    static constexpr uint64_t kRestartWaitNs = 2'000'000'000;
    static constexpr uint64_t kBirdStillNs = 150'000'000;  // the camera has stopped

    float axis(int neg, int neg2, int pos, int pos2) const {
        float v = 0;
        if ((neg >= 0 && down_[neg]) || (neg2 >= 0 && down_[neg2])) v -= 1;
        if ((pos >= 0 && down_[pos]) || (pos2 >= 0 && down_[pos2])) v += 1;
        return v;
    }

    void tap(float x, float y, ModContext& ctx) {
        end_pan(ctx);
        if (pinch_.active()) pinch_.release(ctx);
        tap_ = ctx.input.pointer_down(x, y);
    }

    void grab(const Status& s, ModContext& ctx) {
        end_pan(ctx);
        if (pinch_.active()) pinch_.release(ctx);
        if (s.level != aim_level_) aim_level_ = s.level, last_dx_ = last_dy_ = 0;
        anchor_x_ = s.bird.x;
        anchor_y_ = s.bird.y;
        reach_ = s.reach;
        dx_ = last_dx_;
        dy_ = last_dy_;
        aim_ = ctx.input.pointer_down(anchor_x_, anchor_y_);
    }

    void aim(const Status& s, float dt, ModContext& ctx) {
        if (!down_[kSpace] || !s.playing) {
            ctx.input.pointer_up(aim_);
            aim_ = -1;
            last_dx_ = dx_;
            last_dy_ = dy_;
            return;
        }
        const float speed = (down_[kShift] ? kFineAimSpeed : kAimSpeed) * dt;
        dx_ += axis(kA, kLeft, kD, kRight) * speed;
        dy_ += axis(kW, kUp, kS, kDown) * speed;
        const float length = std::hypot(dx_, dy_);
        if (length > kAimLimit) dx_ *= kAimLimit / length, dy_ *= kAimLimit / length;
        const SurfaceRect& a = ctx.screen;
        const float x = std::clamp(anchor_x_ + dx_ * reach_, a.x, a.x + a.width - 1.0f);
        const float y = std::clamp(anchor_y_ + dy_ * reach_, a.y, a.y + a.height - 1.0f);
        ctx.input.pointer_move(aim_, x, y);
    }

    // The camera follows a finger on the scenery: it moves against the arrows.
    // At the screen's edge the finger lifts and lands again.
    void pan(const Status& s, float dt, ModContext& ctx) {
        const float dx = axis(-1, kLeft, -1, kRight), dy = axis(-1, kUp, -1, kDown);
        if ((dx == 0 && dy == 0) || s.flying || pinch_.active()) return end_pan(ctx);
        const SurfaceRect& a = ctx.screen;
        const float step = kPanSpeed * a.width * dt;
        if (pan_ >= 0) {
            pan_x_ -= dx * step;
            pan_y_ -= dy * step;
            const float margin = a.height * 0.1f;
            if (pan_x_ < a.x + margin || pan_x_ > a.x + a.width - margin || pan_y_ < a.y + margin ||
                pan_y_ > a.y + a.height - margin) {
                return end_pan(ctx);
            }
            ctx.input.pointer_move(pan_, pan_x_, pan_y_);
            return;
        }
        // Land where the scenery is: away from the HUD and the slingshot.
        pan_x_ = a.x + a.width * (0.5f + 0.25f * dx);
        pan_y_ = a.y + a.height * (0.5f + 0.25f * dy);
        if (s.bird.set && std::hypot(pan_x_ - s.bird.x, pan_y_ - s.bird.y) < s.reach * 2) pan_x_ = a.x + a.width * 0.6f;
        pan_ = ctx.input.pointer_down(pan_x_, pan_y_);
    }

    // The held ability touch follows W A S D or the arrows: abilities that aim
    // (Stella's birds) point at the finger.
    void steer(float dt, ModContext& ctx) {
        const float dx = axis(kA, kLeft, kD, kRight), dy = axis(kW, kUp, kS, kDown);
        if (!dx && !dy) return;
        const SurfaceRect& a = ctx.screen;
        const float step = (down_[kShift] ? kFineSteerSpeed : kSteerSpeed) * a.height * dt;
        ability_x_ = std::clamp(ability_x_ + dx * step, a.x, a.x + a.width - 1.0f);
        ability_y_ = std::clamp(ability_y_ + dy * step, a.y, a.y + a.height - 1.0f);
        ctx.input.pointer_move(ability_, ability_x_, ability_y_);
    }

    void end_pan(ModContext& ctx) {
        if (pan_ < 0) return;
        ctx.input.pointer_up(pan_);
        pan_ = -1;
    }

    std::mutex mutex_;
    Status status_;
    unsigned zooms_ = 0;
    uint64_t bird_moved_ns_ = 0;
    bool want_grab_ = false;

    std::array<bool, kKeyCount> down_{}, pressed_{};
    bool back_ = false;  // BACK is down
    uint64_t restart_ns_ = 0;  // R in play: until when to wait for the pause menu
    float restart_x_ = -1, restart_y_ = -1;
    uint64_t last_ns_ = 0;
    int tap_ = -1, ability_ = -1, aim_ = -1, pan_ = -1;
    float anchor_x_ = 0, anchor_y_ = 0, reach_ = 0, dx_ = 0, dy_ = 0, last_dx_ = 0, last_dy_ = 0;
    std::string aim_level_;
    float pan_x_ = 0, pan_y_ = 0, ability_x_ = 0, ability_y_ = 0;
    slingshot::mods::EmulatedPinch pinch_;
};

}  // namespace slingshot::mods::keyboard

#define SLINGSHOT_KEYBOARD_MOD(adapter)                                                                      \
    namespace {                                                                                              \
    struct GameKeyboard final : slingshot::mods::keyboard::Keyboard {                                        \
        GameKeyboard() : Keyboard(adapter) {}                                                                \
    };                                                                                                       \
    }                                                                                                        \
    SLINGSHOT_MOD(GameKeyboard, "keyboard",                                                                  \
                  "Play with the keyboard: Space + W A S D to aim and shoot, Space for abilities, arrow keys " \
                  "and +/- for the camera, R to restart, Enter for the next level")
