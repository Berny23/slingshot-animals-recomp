// SPDX-License-Identifier: GPL-3.0-or-later
// SDL3 platform host: window, GLES2 context and the Android app lifecycle
// (what GLSurfaceView, NativeApplication and its Updater do on Android).
//
// Usage: slingshot [--mod <name>]... [--no-mod <name>]... [--no-mods] [--list-mods]
//                  [--locale <ll_CC>] [--dump-scripts <dir>] [game-dir]
//   game-dir holds lib/ and assets/ from the APK. The mods enabled are the
//   build's default ones (SLINGSHOT_DEFAULT_MODS), or instead those given with
//   --mod or SLINGSHOT_MODS=name,name ("all" for every mod); --no-mod takes one
//   away, --no-mods all of them. The device locale is --locale, else
//   SLINGSHOT_LOCALE, else the host's (LC_ALL, LC_MESSAGES, LANG).
//   --dump-scripts writes the game's scripts and configs, decrypted, into <dir>
//   and exits.
#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "android_input.hpp"
#include "font_provider.hpp"
#include "mod.hpp"
#include "recomp/fusion.hpp"
#include "recomp/gles.hpp"
#include "recomp/java.hpp"
#include "recomp/jni.hpp"
#include "recomp/runtime.hpp"
#include "recomp/vfs.hpp"
#include "sdl_audio.hpp"
#include "sdl_input.hpp"
#include "test_script.hpp"
#include "watchdog.hpp"

#ifndef GAME_DIR_DEFAULT
#define GAME_DIR_DEFAULT "."
#endif

namespace {

namespace jni = recomp::jni;
constexpr const char* kApp = "com/rovio/fusion/NativeApplication";

// com.rovio.fusion.NativeApplication: the game's native methods and the
// lifecycle state machine around them (doInit/doPause/doResume/doShutdown).
struct NativeApp {
    enum class State { Uninitialized, RenderReady, Paused, Exiting };
    jni::Ref self = 0;
    bool render_thread = false;
    State state = State::Uninitialized;

    void init(int w, int h) {
        if (state != State::Uninitialized) return;
        call("nativeInit", {uint32_t(w), uint32_t(h)});
        call("nativeResume");
        state = State::RenderReady;
    }
    // Activity.onPause / onResume (the window was minimized / restored).
    void pause() {
        if (state != State::RenderReady) return;
        call("nativePause");
        state = State::Paused;
    }
    void resume() {
        if (state != State::Paused) return;
        call("nativeResume");
        state = State::RenderReady;
    }
    void resize(int w, int h) {
        if (state != State::Exiting) call("nativeResize", {uint32_t(w), uint32_t(h)});
    }
    // doUpdate(): false when the game wants to exit.
    bool update() {
        if (!(call("nativeUpdate") & 0xff)) state = State::Exiting;
        if (render_thread && state == State::RenderReady) call("nativeRender");
        return state != State::Exiting;
    }
    void shutdown() {
        pause();
        call("nativeDeinit");
        state = State::Exiting;
    }

    uint32_t call(const char* name, std::initializer_list<uint32_t> extra = {}) {
        const uint32_t fn = jni::native_method(kApp, name);
        if (!fn) {
            std::fprintf(stderr, "host: game has no %s\n", name);
            return 0;
        }
        jni::NativeFrame frame;
        uint32_t args[6] = {jni::env(), self};
        size_t n = 2;
        for (uint32_t v : extra) args[n++] = v;
        switch (n) {
            case 2: return recomp::guest_call(fn, {args[0], args[1]});
            case 3: return recomp::guest_call(fn, {args[0], args[1], args[2]});
            default: return recomp::guest_call(fn, {args[0], args[1], args[2], args[3]});
        }
    }
};

void* gl_loader(const char* name) { return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name)); }

struct Options {
    std::string game_dir = GAME_DIR_DEFAULT;
    std::vector<std::string> mods;
    std::string locale;
    std::string dump_dir;  // --dump-scripts
};

// The host's locale as POSIX resolves the message language.
std::string host_locale() {
    for (const char* var : {"SLINGSHOT_LOCALE", "LC_ALL", "LC_MESSAGES", "LANG"})
        if (const char* v = std::getenv(var); v && *v) return v;
    return "C";
}

#ifndef SLINGSHOT_DEFAULT_MODS
#define SLINGSHOT_DEFAULT_MODS ""  // built without mods
#endif

// Appends the mods of a "name,name" list; "all" stands for every mod.
void add_mods(const std::string& list, std::vector<std::string>& mods) {
    std::stringstream ss(list);
    for (std::string m; std::getline(ss, m, ',');) {
        if (m == "all") {
            for (const auto& info : slingshot::available_mods()) mods.push_back(info.name);
        } else if (!m.empty()) {
            mods.push_back(m);
        }
    }
}

// The mods built into this executable; "*" marks the ones enabled by default.
void list_mods() {
    const auto& mods = slingshot::available_mods();
    if (mods.empty()) {
        std::printf("No mods are built into this executable.\n");
        return;
    }
    std::vector<std::string> defaults;
    add_mods(SLINGSHOT_DEFAULT_MODS, defaults);
    std::printf("Mods built into this executable (* = enabled by default):\n");
    for (const auto& m : mods) {
        bool on = false;
        for (const std::string& d : defaults) on |= d == m.name;
        std::printf("%c %-20s %s\n", on ? '*' : ' ', m.name, m.description);
    }
}

constexpr char kUsage[] =
    "usage: %s [--mod <name>]... [--no-mod <name>]... [--no-mods] [--list-mods] [--locale <ll_CC>] "
    "[--dump-scripts <dir>] [game-dir]\n";

// Returns false when the program should exit right away (help, mod list).
bool parse_args(int argc, char** argv, Options& opt, int& status) {
    std::vector<std::string> chosen, removed;
    bool any_chosen = false, none = false;
    if (const char* env = std::getenv("SLINGSHOT_MODS")) {
        add_mods(env, chosen);
        any_chosen = true;
    }
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--mod") && i + 1 < argc) {
            add_mods(argv[++i], chosen);
            any_chosen = true;
        } else if (!std::strcmp(argv[i], "--no-mod") && i + 1 < argc) {
            removed.push_back(argv[++i]);
        } else if (!std::strcmp(argv[i], "--no-mods")) {
            none = true;
        } else if (!std::strcmp(argv[i], "--locale") && i + 1 < argc) {
            opt.locale = argv[++i];
        } else if (!std::strcmp(argv[i], "--dump-scripts") && i + 1 < argc) {
            opt.dump_dir = argv[++i];
        } else if (!std::strcmp(argv[i], "--list-mods")) {
            list_mods();
            status = 0;
            return false;
        } else if (argv[i][0] == '-') {
            std::fprintf(stderr, kUsage, argv[0]);
            status = 2;
            return false;
        } else {
            opt.game_dir = argv[i];
        }
    }
    if (!any_chosen) add_mods(SLINGSHOT_DEFAULT_MODS, chosen);
    for (const std::string& r : removed) {
        bool known = false;
        for (const auto& info : slingshot::available_mods()) known |= r == info.name;
        if (!known) {
            std::fprintf(stderr, "host: unknown mod '%s' (see --list-mods)\n", r.c_str());
            status = 1;
            return false;
        }
    }
    for (const std::string& m : none ? std::vector<std::string>{} : chosen) {
        bool dropped = false, seen = false;
        for (const std::string& r : removed) dropped |= r == m;
        for (const std::string& o : opt.mods) seen |= o == m;
        if (!dropped && !seen) opt.mods.push_back(m);
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    int status = 0;
    if (!parse_args(argc, argv, opt, status)) return status;
    std::vector<std::unique_ptr<slingshot::Mod>> mods;
    if (!slingshot::load_mods(opt.mods, mods)) return 1;
    recomp::vfs_set_asset_root(opt.game_dir + "/assets");
    recomp::java::set_locale(recomp::java::parse_locale(opt.locale.empty() ? host_locale() : opt.locale));

    // Mouse and touch are separate inputs: no synthetic events in either direction.
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "host: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_Window* window = SDL_CreateWindow("Slingshot Animals (recompiled)", 1280, 720,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) {
        std::fprintf(stderr, "host: cannot create window: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) {
        std::fprintf(stderr, "host: cannot create a GLES 2 context: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetSwapInterval(1);
    recomp::gles::set_loader(gl_loader);
    {
        recomp::java::Display d;
        SDL_GetWindowSizeInPixels(window, &d.width, &d.height);
        d.dpi = 160.0f * SDL_GetWindowDisplayScale(window);
        recomp::java::set_display(d);
    }

    slingshot::SdlAudio audio;
    recomp::java::set_audio_backend(&audio);
    slingshot::FontconfigProvider fonts;
    recomp::java::set_font_provider(&fonts);

    if (!recomp::runtime_init(opt.game_dir + "/lib/libAngryBirdsClassic.so")) return 1;
    recomp::run_constructors();
    if (const uint32_t onload = recomp::lib_symbol("JNI_OnLoad")) {
        jni::NativeFrame frame;
        recomp::guest_call(onload, {jni::vm(), 0});
    }

    // NativeApplication(GLSurfaceView, filesDir)
    NativeApp app;
    app.self = jni::new_global(jni::new_object(kApp));
    {
        jni::NativeFrame frame;
        app.call("nativeConfig", {jni::new_string(recomp::java::files_dir())});
    }
    const uint32_t orientations = app.call("nativeGetPossibleOrientations");
    app.render_thread = app.call("nativeRenderThread") & 0xff;
    std::fprintf(stderr, "host: orientations 0x%x, render thread %d\n", orientations, app.render_thread);

    // Renderer.onSurfaceChanged -> Updater.initialize / onResize
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    app.init(w, h);
    app.resize(w, h);

    if (!opt.dump_dir.empty()) {
        jni::NativeFrame frame;
        const int n = recomp::hle::dump_encrypted_assets(opt.dump_dir);
        if (n < 0) std::fprintf(stderr, "host: cannot dump the scripts\n");
        else std::printf("wrote %d files to %s\n", n, opt.dump_dir.c_str());
        std::fflush(stdout);
        std::_Exit(n < 0 ? 1 : 0);  // the engine is in no state for an orderly shutdown
    }

    slingshot::AndroidInput input;
    slingshot::SdlInput sdl_input(window, input);
    slingshot::TestScript script(window);
    slingshot::Overlay overlay;
    slingshot::Watchdog watchdog;
    long frame = 0;
    bool running = true;
    while (running) {
        ++frame;
        if (!script.run_input(frame)) running = false;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        slingshot::ModContext ctx{input, window, w, h, SDL_GetWindowPixelDensity(window), SDL_GetTicksNS()};
        SDL_Event e;
        // Paused (minimized): nothing is updated or drawn, so wait for events.
        if (app.state == NativeApp::State::Paused) {
            if (script.active()) SDL_Delay(16);  // keep script frames at about the display rate
            else SDL_WaitEvent(nullptr);
        }
        while (SDL_PollEvent(&e)) {
            bool consumed = false;
            for (auto& m : mods) consumed = consumed || m->on_event(e, ctx);
            if (consumed) continue;
            switch (e.type) {
                case SDL_EVENT_QUIT: running = false; break;
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: app.resize(e.window.data1, e.window.data2); break;
                case SDL_EVENT_WINDOW_MINIMIZED: app.pause(); break;
                case SDL_EVENT_WINDOW_RESTORED: app.resume(); break;
                default: sdl_input.handle(e); break;
            }
        }
        for (auto& m : mods) m->on_frame(ctx);

        if (app.state != NativeApp::State::RenderReady) continue;

        // NativeApplication.doUpdate(): MyInputHandler.handleEvents(), then nativeUpdate().
        // Callbacks the Java side posted (runOnAppThread) run first.
        watchdog.frame_started();
        recomp::java::run_posted();
        input.deliver();
        if (!app.update()) running = false;
        watchdog.frame_finished();
        for (auto& m : mods) m->on_overlay(overlay, ctx);
        overlay.render(w, h);
        script.run_shots(frame);
        SDL_GL_SwapWindow(window);
    }
    app.shutdown();
    audio.stop();
    recomp::java::set_audio_backend(nullptr);
    recomp::java::set_font_provider(nullptr);
    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
