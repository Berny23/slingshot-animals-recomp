// SPDX-License-Identifier: GPL-3.0-or-later
// gamepad-pointer: play with a gamepad.
//
// The game only reacts to touches and BACK, so the gamepad becomes a touch pointer:
//   left stick       move the cursor (right stick: slower, for fine aiming)
//   A                touch at the cursor; hold and move to drag (aim the slingshot)
//   B, Start         BACK (pause, close dialogs)
//   left / right trigger   zoom out / in (emulated pinch)
// The gamepad's own events are not passed on to the game.
#include <algorithm>
#include <cmath>

#include "common/pinch.hpp"
#include "mod.hpp"

namespace {

using slingshot::ModContext;
using slingshot::SurfaceRect;
namespace android = slingshot::android;

constexpr float kDeadzone = 0.15f;
constexpr float kFastSpeed = 1.1f;  // cursor speed at full deflection, surface heights per second
constexpr float kSlowSpeed = 0.25f;
constexpr float kZoomRate = 1.2f;   // natural log of the finger distance change per second at full trigger
constexpr uint64_t kPinchIdleNs = 100'000'000;
constexpr uint64_t kCursorTimeoutNs = 5'000'000'000;  // hide the cursor when the gamepad is idle

float shaped(float v) {
    const float a = std::fabs(v);
    if (a < kDeadzone) return 0;
    const float t = (a - kDeadzone) / (1 - kDeadzone);
    return std::copysign(t * t, v);
}

float axis_value(Sint16 v) { return std::clamp(v / 32767.0f, -1.0f, 1.0f); }

class GamepadPointer final : public slingshot::Mod {
public:
    bool on_event(const SDL_Event& e, ModContext& ctx) override {
        switch (e.type) {
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                if (e.gaxis.axis < SDL_GAMEPAD_AXIS_COUNT) axes_[e.gaxis.axis] = axis_value(e.gaxis.value);
                touched(ctx);
                return true;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
                button(e.gbutton.button, e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN, ctx);
                touched(ctx);
                return true;
            case SDL_EVENT_MOUSE_MOTION:
                shown_ = false;  // the mouse is in use
                return false;
            default:
                return false;
        }
    }

    void on_frame(ModContext& ctx) override {
        const SurfaceRect& a = ctx.screen;
        if (!placed_) {
            x_ = a.x + a.width * 0.5f;
            y_ = a.y + a.height * 0.5f;
            placed_ = true;
        }
        const float dt = last_ns_ ? std::min((ctx.now_ns - last_ns_) * 1e-9f, 0.1f) : 0.0f;
        last_ns_ = ctx.now_ns;

        const float fast = kFastSpeed * a.height * dt, slow = kSlowSpeed * a.height * dt;
        const float dx = shaped(axes_[SDL_GAMEPAD_AXIS_LEFTX]) * fast + shaped(axes_[SDL_GAMEPAD_AXIS_RIGHTX]) * slow;
        const float dy = shaped(axes_[SDL_GAMEPAD_AXIS_LEFTY]) * fast + shaped(axes_[SDL_GAMEPAD_AXIS_RIGHTY]) * slow;
        // Kept on the game's screen, also when it moved (bars, window size).
        const float x = std::clamp(x_ + dx, a.x, a.x + a.width - 1.0f), y = std::clamp(y_ + dy, a.y, a.y + a.height - 1.0f);
        if (x != x_ || y != y_) {
            x_ = x;
            y_ = y;
            if (pointer_ >= 0) ctx.input.pointer_move(pointer_, x_, y_);
        }

        // Triggers: values are 0..1 (SDL reports them as 0..32767).
        const float zoom = std::max(0.0f, axes_[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER]) -
                           std::max(0.0f, axes_[SDL_GAMEPAD_AXIS_LEFT_TRIGGER]);
        if (std::fabs(zoom) > kDeadzone && pointer_ < 0) pinch_.zoom(std::exp(zoom * kZoomRate * dt), x_, y_, ctx.now_ns);
        pinch_.update(ctx, kPinchIdleNs);

        if (shown_ && ctx.now_ns - last_use_ns_ > kCursorTimeoutNs && pointer_ < 0) shown_ = false;
    }

    void on_overlay(slingshot::Overlay& overlay, ModContext& ctx) override {
        if (!shown_) return;
        const float r = std::max(8.0f, ctx.screen.height * 0.018f);
        overlay.ring(x_, y_, r + 2, 0, 0x00000060);  // shadow for contrast on bright backgrounds
        overlay.ring(x_, y_, r, pointer_ >= 0 ? 0 : r * 0.55f, 0xffffffe0);
    }

private:
    void touched(ModContext& ctx) {
        last_use_ns_ = ctx.now_ns;
        shown_ = true;
    }

    void button(Uint8 b, bool down, ModContext& ctx) {
        switch (b) {
            case SDL_GAMEPAD_BUTTON_SOUTH:
                if (down && pointer_ < 0) {
                    if (pinch_.active()) pinch_.release(ctx);
                    pointer_ = ctx.input.pointer_down(x_, y_);
                } else if (!down && pointer_ >= 0) {
                    ctx.input.pointer_up(pointer_);
                    pointer_ = -1;
                }
                break;
            case SDL_GAMEPAD_BUTTON_EAST:
            case SDL_GAMEPAD_BUTTON_START:
                ctx.input.key(down, android::key::kBack, 0, android::kBuiltInKeyboard);
                break;
            default:
                break;
        }
    }

    float axes_[SDL_GAMEPAD_AXIS_COUNT] = {};
    float x_ = 0, y_ = 0;
    bool placed_ = false, shown_ = false;
    int pointer_ = -1;
    uint64_t last_ns_ = 0, last_use_ns_ = 0;
    slingshot::mods::EmulatedPinch pinch_;
};

}  // namespace

SLINGSHOT_MOD(GamepadPointer, "gamepad-pointer", "Play with a gamepad: stick moves a cursor, A touches, triggers zoom");
