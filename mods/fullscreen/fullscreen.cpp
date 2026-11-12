// SPDX-License-Identifier: GPL-3.0-or-later
// fullscreen: F11 toggles fullscreen. The game handles it as a window resize.
#include "mod.hpp"

namespace {

class Fullscreen final : public slingshot::Mod {
public:
    bool on_event(const SDL_Event& e, slingshot::ModContext& ctx) override {
        if ((e.type != SDL_EVENT_KEY_DOWN && e.type != SDL_EVENT_KEY_UP) || e.key.scancode != SDL_SCANCODE_F11) return false;
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
            const bool full = SDL_GetWindowFlags(ctx.window) & SDL_WINDOW_FULLSCREEN;
            SDL_SetWindowFullscreen(ctx.window, !full);
        }
        return true;
    }
};

}  // namespace

SLINGSHOT_MOD(Fullscreen, "fullscreen", "F11 switches between window and fullscreen");
