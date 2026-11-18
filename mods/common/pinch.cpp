// SPDX-License-Identifier: GPL-3.0-or-later
#include "pinch.hpp"

#include <algorithm>
#include <cmath>

namespace slingshot::mods {
namespace {

// Half finger distances relative to the shorter surface side.
constexpr float kMinSpread = 0.04f, kMaxSpread = 0.45f;
constexpr float kStartSmall = 0.10f;  // when zooming in ...
constexpr float kStartLarge = 0.35f;  // ... and when zooming out
constexpr float kFollow = 0.35f;      // fraction of the remaining distance moved per frame

}  // namespace

void EmulatedPinch::zoom(float factor, float cx, float cy, uint64_t now_ns) {
    if (factor <= 0) return;
    pending_ += std::log(factor);
    if (!active_) {
        cx_ = cx;
        cy_ = cy;
    }
    last_request_ns_ = now_ns;
}

void EmulatedPinch::update(ModContext& ctx, uint64_t idle_ns) {
    const SurfaceRect& a = ctx.screen;
    const float side = std::min(a.width, a.height);
    if (!active_) {
        if (pending_ == 0) return;
        if (ctx.input.active_pointers()) {  // the player is touching already
            pending_ = 0;
            return;
        }
        spread_ = target_ = side * (pending_ > 0 ? kStartSmall : kStartLarge);
        cx_ = std::clamp(cx_, a.x + spread_, a.x + a.width - spread_);
        cy_ = std::clamp(cy_, a.y, a.y + a.height - 1.0f);
        ids_[0] = ctx.input.pointer_down(cx_ - spread_, cy_);
        ids_[1] = ctx.input.pointer_down(cx_ + spread_, cy_);
        active_ = ids_[0] >= 0 && ids_[1] >= 0;
        if (!active_) release(ctx);
        return;  // move from the next frame on, after the game saw both fingers land
    }

    // Apply as much of the request as the limits allow; the rest waits for a new grip.
    if (pending_ != 0) {
        const float lo = std::log(side * kMinSpread / target_), hi = std::log(side * kMaxSpread / target_);
        const float step = std::clamp(pending_, std::min(lo, 0.0f), std::max(hi, 0.0f));
        target_ *= std::exp(step);
        pending_ -= step;
        if (std::fabs(pending_) < 1e-4f) pending_ = 0;
    }
    spread_ += (target_ - spread_) * kFollow;
    if (std::fabs(target_ - spread_) < 0.5f) spread_ = target_;
    ctx.input.pointer_move(ids_[0], std::max(a.x, cx_ - spread_), cy_);
    ctx.input.pointer_move(ids_[1], std::min(a.x + a.width - 1.0f, cx_ + spread_), cy_);

    const bool settled = spread_ == target_;
    const bool blocked = pending_ != 0;  // at a limit: lift and grip again
    if (settled && (blocked || ctx.now_ns - last_request_ns_ > idle_ns)) release(ctx);
}

void EmulatedPinch::release(ModContext& ctx) {
    for (int& id : ids_) {
        ctx.input.pointer_up(id);
        id = -1;
    }
    active_ = false;
}

}  // namespace slingshot::mods
