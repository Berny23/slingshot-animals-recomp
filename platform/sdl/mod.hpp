// SPDX-License-Identifier: GPL-3.0-or-later
// Mod API of the desktop host.
//
// The base port behaves like the original app on an Android device. Anything
// beyond that (new controls, conveniences, changed behaviour) is a mod: a
// class registered with SLINGSHOT_MOD in mods/<name>/, built into the
// executable. SLINGSHOT_MOD_OFF registers one that is off by default. The ones
// in SLINGSHOT_DEFAULT_MODS (a build setting, "default" = every mod not
// registered off) start enabled; --mod, --enable-mod, --disable-mod and SLINGSHOT_MODS
// change that.
//
// Mods see every SDL event before the default translation and talk to the
// game through the same AndroidInput the base port uses, so they can only
// produce input a real device could produce too.
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "android_input.hpp"
#include "overlay.hpp"

namespace slingshot {

// A rectangle on the surface, in pixels from the top left.
struct SurfaceRect {
    float x, y, width, height;
};

struct ModContext {
    AndroidInput& input;
    SDL_Window* window;
    int width, height;    // surface size in pixels
    float pixel_density;  // surface pixels per window coordinate (SDL event positions)
    uint64_t now_ns;      // SDL_GetTicksNS() of the current frame
    float screen_scale;   // surface pixels per game pixel (screen_scale of a mod, else 1)
    SurfaceRect screen;   // where the game's screen is (the whole surface unless screen_aspect bars it)
};

class Mod {
public:
    virtual ~Mod() = default;
    // Called for each SDL event before the host translates it; return true to consume it.
    virtual bool on_event(const SDL_Event& e, ModContext& ctx) { return false; }
    // Called once per frame, before the input of the frame is delivered to the game.
    virtual void on_frame(ModContext& ctx) {}
    // Called after the game rendered the frame: shapes to draw over it.
    virtual void on_overlay(Overlay& overlay, ModContext& ctx) {}
    // Called once at start with the display (pixels, dots per inch): surface pixels
    // per game pixel, or 0 to leave the size alone. The game then sees a screen of
    // the display's size divided by it and is drawn scaled to the surface.
    virtual float screen_scale(int width, int height, float dpi) { return 0; }
    // Called when the window's size or fullscreen state changes: the width / height
    // ratio the game's screen is held at, or 0 for the window's own. The screen is
    // then centred in the window with black bars.
    virtual float screen_aspect(bool fullscreen) { return 0; }
    // Called once before the window is created: true to open it in fullscreen.
    virtual bool start_fullscreen() { return false; }
};

struct ModInfo {
    const char* name;
    const char* description;
    std::unique_ptr<Mod> (*create)();
    bool on_by_default;
};

bool register_mod(const ModInfo& info);
const std::vector<ModInfo>& available_mods();

// Instantiates the named mods; prints an error and returns false for unknown names.
bool load_mods(const std::vector<std::string>& names, std::vector<std::unique_ptr<Mod>>& out);

// Mod options: --mod-option <mod>.<key>=<value>, set before the mods are created.
// mod_option returns the value, or nullptr when it was not given.
void set_mod_option(const std::string& mod, const std::string& key, const std::string& value);
const std::string* mod_option(const std::string& mod, const std::string& key);

}  // namespace slingshot

#define SLINGSHOT_MOD_REGISTER(Type, name, description, on)                                     \
    static const bool slingshot_mod_registered_##Type = ::slingshot::register_mod(             \
        {name, description, []() -> std::unique_ptr<::slingshot::Mod> { return std::make_unique<Type>(); }, on})
#define SLINGSHOT_MOD(Type, name, description) SLINGSHOT_MOD_REGISTER(Type, name, description, true)
#define SLINGSHOT_MOD_OFF(Type, name, description) SLINGSHOT_MOD_REGISTER(Type, name, description, false)
