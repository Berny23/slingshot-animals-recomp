// SPDX-License-Identifier: GPL-3.0-or-later
// fullscreen: F11 toggles fullscreen. The game handles it as a window resize.
// --mod-option fullscreen.aspect=<w>:<h> holds the game at that aspect ratio in
// fullscreen (black bars on wider or taller screens).
#include <cstdio>
#include <cstdlib>

#include "mod.hpp"

namespace {

// "16:9", "21:9" or a ratio such as "1.7778"; 0 if not given or invalid.
float parse_aspect() {
    const std::string* v = slingshot::mod_option("fullscreen", "aspect");
    if (!v) return 0;
    float w = 0, h = 0;
    if (std::sscanf(v->c_str(), "%f:%f", &w, &h) == 2 && w > 0 && h > 0) return w / h;
    if (const float r = std::strtof(v->c_str(), nullptr); r > 0 && v->find(':') == std::string::npos) return r;
    std::fprintf(stderr, "fullscreen: ignoring aspect \"%s\" (expected <w>:<h>)\n", v->c_str());
    return 0;
}

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
    float screen_aspect(bool fullscreen) override { return fullscreen ? aspect_ : 0; }

private:
    const float aspect_ = parse_aspect();
};

}  // namespace

SLINGSHOT_MOD(Fullscreen, "fullscreen", "F11 switches between window and fullscreen");
