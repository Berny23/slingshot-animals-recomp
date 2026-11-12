// SPDX-License-Identifier: GPL-3.0-or-later
// Shared mod helper: a two-finger pinch on the emulated touch screen.
//
// zoom() requests a change of the finger distance; update() moves the fingers
// there smoothly, one step per frame. The fingers land on the first request,
// lift once they settled and no request came for a while, and lift and grip
// again when the distance hits the screen-size limits, as real fingers would.
#pragma once

#include <cstdint>

#include "mod.hpp"

namespace slingshot::mods {

class EmulatedPinch {
public:
    // factor > 1 spreads the fingers (zoom in), < 1 pinches them (zoom out),
    // centered on (cx, cy) in surface pixels.
    void zoom(float factor, float cx, float cy, uint64_t now_ns);
    // Call once per frame; idle_ns is how long settled fingers stay down.
    void update(ModContext& ctx, uint64_t idle_ns);
    void release(ModContext& ctx);
    bool active() const { return active_; }

private:
    bool active_ = false;
    int ids_[2] = {-1, -1};
    float cx_ = 0, cy_ = 0;
    float spread_ = 0, target_ = 0;  // half finger distance, pixels
    float pending_ = 0;              // requested change not applied yet, natural log of the factor
    uint64_t last_request_ns_ = 0;
};

}  // namespace slingshot::mods
