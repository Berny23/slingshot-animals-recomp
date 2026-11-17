// SPDX-License-Identifier: GPL-3.0-or-later
// Star Wars draws its sprites and fonts at their own size, without scaling: it
// picks the set for the screen's width (2048x1536 from 2048 pixels on), so on a
// wide phone everything is too large, and with the next set too small. On phones
// (a screen diagonal under 7 inches) the game sees a screen 1536 pixels high,
// the height the 2048x1536 set was made for, and is drawn scaled to the real
// one, as the other games scale their sets.
#include <algorithm>
#include <cmath>

#include "mod.hpp"

namespace {

constexpr double kTabletInches = 7.0;
constexpr float kGameHeight = 1536;

class PhoneScale final : public slingshot::Mod {
public:
    float screen_scale(int width, int height, float dpi) override {
        if (dpi <= 0 || std::hypot(double(width), double(height)) / dpi >= kTabletInches) return 0;
        return float(std::min(width, height)) / kGameHeight;
    }
};

}  // namespace

SLINGSHOT_MOD(PhoneScale, "phone-scale",
              "On phones the game is laid out for a tablet-sized screen with its sharpest sprites and scaled to fit");
