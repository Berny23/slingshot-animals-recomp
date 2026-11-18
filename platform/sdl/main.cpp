// SPDX-License-Identifier: GPL-3.0-or-later
// SDL3 platform host: window, GLES2 context and the Android app lifecycle
// (what GLSurfaceView, NativeApplication and its Updater do on Android).
//
// Usage: slingshot-<game> [--mod <name>]... [--no-mod <name>]... [--no-mods] [--list-mods]
//                         [--mod-option <mod>.<key>=<value>]... [--locale <ll_CC>]
//                         [--dump-scripts <dir>] [game-dir]
//   game-dir holds lib/, assets/ and icon.png from the APK (default: game/ next to the executable, else where the
//   build extracted it). The mods enabled are the build's default ones (SLINGSHOT_DEFAULT_MODS), or instead those given with
//   --mod or SLINGSHOT_MODS=name,name ("all" for every mod); --no-mod takes one
//   away, --no-mods all of them. --mod-option passes a setting to a mod (see
//   README.md for each mod's). The device locale is --locale, else
//   SLINGSHOT_LOCALE, else the host's (LC_ALL, LC_MESSAGES, LANG).
//   --dump-scripts writes the game's scripts and configs, decrypted, into <dir>
//   and exits.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#ifdef __ANDROID__
#include <jni.h>
#endif
#ifdef __SWITCH__
#include <pthread.h>

#include "switch_host.hpp"
#endif
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "android_input.hpp"
#include "font_provider.hpp"
#include "font_scan.hpp"
#include "mod.hpp"
#include "recomp/fusion.hpp"
#include "recomp/game.hpp"
#include "recomp/gles.hpp"
#include "recomp/java.hpp"
#include "recomp/jni.hpp"
#include "recomp/runtime.hpp"
#include "recomp/vfs.hpp"
#include "sdl_audio.hpp"
#include "sdl_input.hpp"
#include "test_script.hpp"
#include "watchdog.hpp"

namespace {

namespace jni = recomp::jni;
// com.rovio.fusion.NativeApplication (MyRenderer in older games): the game's
// native methods and the lifecycle state machine around them
// (doInit/doPause/doResume/doShutdown).
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
        if (state == State::Exiting) return;
        call("nativeResize", {uint32_t(w), uint32_t(h)});
        resolution_changed();
    }
    // Stella's nativeResize only resizes the GL context. The game takes the new size
    // (Lua's screenWidth/screenHeight, which its scripts watch to lay out again) in
    // the virtual framework::App::resolutionChanged(), which its Android shell never calls.
    void resolution_changed() {
        static const uint32_t get = recomp::lib_symbol("_ZN9framework3App3getEv");
        // Its offset in the vtable: where the base class has its empty implementation.
        static const int slot = [] {
            const uint32_t vtable = recomp::lib_symbol("_ZTVN9framework3AppE");
            const uint32_t base = recomp::lib_symbol("_ZN9framework3App17resolutionChangedEv");
            if (!vtable || !base) return -1;
            for (uint32_t off = 0; off < 0x100; off += 4)
                if ((recomp::ld32(vtable + 8 + off) & ~1u) == (base & ~1u)) return int(off);
            return -1;
        }();
        if (!get || slot < 0) return;
        jni::NativeFrame frame;
        if (const uint32_t app = recomp::guest_call(get))
            recomp::guest_call(recomp::ld32(recomp::ld32(app) + uint32_t(slot)), {app});
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
        const uint32_t fn = jni::native_method(recomp::game.app_class, name);
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

#ifdef __ANDROID__
// The APK carries game/ (library, assets, icon) and game/files.txt, the list of its
// files. The first start after an install or update copies them to internal storage,
// where the runtime reads them like files on other systems.
std::string android_game_dir() {
    const std::string dir = std::string(SDL_GetAndroidInternalStoragePath()) + "/game";
    size_t size = 0;
    char* list = static_cast<char*>(SDL_LoadFile("game/files.txt", &size));
    if (!list) return dir;
    const std::string files(list, size);
    SDL_free(list);
    const std::string stamp = dir + "/files.txt";
    if (char* old = static_cast<char*>(SDL_LoadFile(stamp.c_str(), &size))) {
        const bool same = std::string(old, size) == files;
        SDL_free(old);
        if (same) return dir;
    }
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::istringstream lines(files);
    for (std::string name; std::getline(lines, name);) {
        if (name.empty()) continue;
        void* data = SDL_LoadFile(("game/" + name).c_str(), &size);
        const std::filesystem::path out = std::filesystem::path(dir) / name;
        std::filesystem::create_directories(out.parent_path(), ec);
        if (!data || !SDL_SaveFile(out.string().c_str(), data, size)) {
            std::fprintf(stderr, "host: cannot copy game/%s: %s\n", name.c_str(), SDL_GetError());
            SDL_free(data);
            return dir;
        }
        SDL_free(data);
    }
    SDL_SaveFile(stamp.c_str(), files.data(), files.size());  // last: marks a complete copy
    return dir;
}

// DeviceInfoWrapper's values: Display.getRealMetrics (the whole screen, system bars
// included) in landscape, the activity's orientation, and the mean of xdpi and ydpi.
bool android_display(recomp::java::Display& d) {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) return false;
    jclass act = env->GetObjectClass(activity);
    jobject wm = env->CallObjectMethod(activity, env->GetMethodID(act, "getWindowManager", "()Landroid/view/WindowManager;"));
    jclass wm_cls = env->FindClass("android/view/WindowManager");
    jobject disp = env->CallObjectMethod(wm, env->GetMethodID(wm_cls, "getDefaultDisplay", "()Landroid/view/Display;"));
    jclass dm_cls = env->FindClass("android/util/DisplayMetrics");
    jobject dm = env->NewObject(dm_cls, env->GetMethodID(dm_cls, "<init>", "()V"));
    jclass disp_cls = env->FindClass("android/view/Display");
    env->CallVoidMethod(disp, env->GetMethodID(disp_cls, "getRealMetrics", "(Landroid/util/DisplayMetrics;)V"), dm);
    const bool ok = !env->ExceptionCheck();
    env->ExceptionClear();
    if (ok) {
        const int w = env->GetIntField(dm, env->GetFieldID(dm_cls, "widthPixels", "I"));
        const int h = env->GetIntField(dm, env->GetFieldID(dm_cls, "heightPixels", "I"));
        d.width = w > h ? w : h;
        d.height = w > h ? h : w;
        d.dpi = (env->GetFloatField(dm, env->GetFieldID(dm_cls, "xdpi", "F")) +
                 env->GetFloatField(dm, env->GetFieldID(dm_cls, "ydpi", "F"))) / 2;
    }
    for (jobject o : {jobject(act), wm, jobject(wm_cls), disp, jobject(dm_cls), dm, jobject(disp_cls)}) env->DeleteLocalRef(o);
    env->DeleteLocalRef(activity);
    return ok && d.width > 0 && d.height > 0;
}
#endif

// game/ next to the executable (in an installed game), else the build's directory.
// On the Switch the NRO's romfs holds them.
std::string default_game_dir() {
#ifdef __ANDROID__
    return android_game_dir();
#elif defined(__SWITCH__)
    return "romfs:";
#endif
    const std::string lib = std::string("/lib/lib") + recomp::game.library + ".so";
    if (const char* base = SDL_GetBasePath()) {
        const std::string dir = std::string(base) + "game";
        std::error_code ec;
        if (std::filesystem::exists(dir + lib, ec)) return dir;
    }
    return recomp::game.game_dir;
}

struct Options {
    std::string game_dir = default_game_dir();
    std::vector<std::string> mods;
    std::string locale;
    std::string dump_dir;  // --dump-scripts
};

// The host's locale as POSIX resolves the message language, else the system's
// first preferred language (Windows, macOS apps, Android).
std::string host_locale() {
    for (const char* var : {"SLINGSHOT_LOCALE", "LC_ALL", "LC_MESSAGES", "LANG"})
        if (const char* v = std::getenv(var); v && *v) return v;
    int n = 0;
    std::string locale = "C";
    if (SDL_Locale** list = SDL_GetPreferredLocales(&n)) {
        if (n > 0 && list[0]->language) {
            locale = list[0]->language;
            if (list[0]->country) locale += std::string("_") + list[0]->country;
        }
        SDL_free(list);
    }
    return locale;
}

// Appends the mods of a "name,name" list; "all" stands for every mod. With
// `known_only`, names of mods this game does not have are skipped.
void add_mods(const std::string& list, std::vector<std::string>& mods, bool known_only = false) {
    std::stringstream ss(list);
    for (std::string m; std::getline(ss, m, ',');) {
        if (m == "all") {
            for (const auto& info : slingshot::available_mods()) mods.push_back(info.name);
        } else if (!m.empty()) {
            bool known = !known_only;
            for (const auto& info : slingshot::available_mods()) known |= m == info.name;
            if (known) mods.push_back(m);
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
    add_mods(recomp::game.default_mods, defaults, true);
    std::printf("Mods built into this executable (* = enabled by default):\n");
    for (const auto& m : mods) {
        bool on = false;
        for (const std::string& d : defaults) on |= d == m.name;
        std::printf("%c %-20s %s\n", on ? '*' : ' ', m.name, m.description);
    }
}

constexpr char kUsage[] =
    "usage: %s [--help] [--mod <name>]... [--no-mod <name>]... [--no-mods] [--list-mods] "
    "[--mod-option <mod>.<key>=<value>]... [--locale <ll_CC>] [--dump-scripts <dir>] [game-dir]\n";

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
        } else if (!std::strcmp(argv[i], "--mod-option") && i + 1 < argc) {
            const std::string arg = argv[++i];
            const size_t dot = arg.find('.'), eq = arg.find('=');
            if (dot == std::string::npos || eq == std::string::npos || dot == 0 || eq < dot + 2) {
                std::fprintf(stderr, "host: --mod-option takes <mod>.<key>=<value>, not '%s'\n", arg.c_str());
                status = 2;
                return false;
            }
            const std::string mod = arg.substr(0, dot);
            bool known = false;
            for (const auto& info : slingshot::available_mods()) known |= mod == info.name;
            if (!known) {
                std::fprintf(stderr, "host: unknown mod '%s' (see --list-mods)\n", mod.c_str());
                status = 1;
                return false;
            }
            slingshot::set_mod_option(mod, arg.substr(dot + 1, eq - dot - 1), arg.substr(eq + 1));
        } else if (!std::strcmp(argv[i], "--locale") && i + 1 < argc) {
            opt.locale = argv[++i];
        } else if (!std::strcmp(argv[i], "--dump-scripts") && i + 1 < argc) {
            opt.dump_dir = argv[++i];
        } else if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
            std::printf(kUsage, argv[0]);
            status = 0;
            return false;
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
    if (!any_chosen) add_mods(recomp::game.default_mods, chosen, true);
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

int run(int argc, char** argv) {
    Options opt;
    int status = 0;
    if (!parse_args(argc, argv, opt, status)) return status;
    std::vector<std::unique_ptr<slingshot::Mod>> mods;
    if (!slingshot::load_mods(opt.mods, mods)) return 1;
    recomp::vfs_set_asset_root(opt.game_dir + "/assets");
#if !defined(__linux__) || defined(__ANDROID__)
    // Saves: the system's place for app data (on Linux $XDG_DATA_HOME, the runtime's default).
    if (char* pref = SDL_GetPrefPath(nullptr, "slingshot-animals-recomp")) {
        recomp::vfs_set_root(std::string(pref) + recomp::game.id);
        SDL_free(pref);
    }
#endif
    recomp::java::set_locale(recomp::java::parse_locale(opt.locale.empty() ? host_locale() : opt.locale));

    // The identifier matches the installed slingshot-<id>.desktop (the taskbar icon on Wayland).
    const std::string app_id = std::string("slingshot-") + recomp::game.id;
    SDL_SetAppMetadata(recomp::game.title, recomp::game.version, app_id.c_str());
    // Mouse and touch are separate inputs: no synthetic events in either direction.
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    // Linear output as on Android: Windows drivers otherwise may pick an sRGB framebuffer
    // that brightens every colour.
    SDL_SetHint(SDL_HINT_OPENGL_FORCE_SRGB_FRAMEBUFFER, "0");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "host: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    // OpenGL ES 2.0 as on Android; desktop OpenGL 2.1 where there is none (macOS, some
    // Windows drivers) or with SLINGSHOT_DESKTOP_GL=1.
    bool desktop_gl = std::getenv("SLINGSHOT_DESKTOP_GL") != nullptr;
    SDL_Window* window = nullptr;
    SDL_GLContext gl = nullptr;
    auto open_window = [&](bool desktop) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            desktop ? SDL_GL_CONTEXT_PROFILE_COMPATIBILITY : SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, desktop ? 1 : 0);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
#if defined(__ANDROID__) || defined(__SWITCH__)
        // Immersive: the game covers the system bars, as the original's full-screen activity.
        constexpr SDL_WindowFlags mode = SDL_WINDOW_FULLSCREEN;
#else
        SDL_WindowFlags mode = SDL_WINDOW_RESIZABLE;
        for (auto& m : mods)
            if (m->start_fullscreen()) mode |= SDL_WINDOW_FULLSCREEN;
#endif
        window = SDL_CreateWindow(recomp::game.title, 1280, 720,
                                  SDL_WINDOW_OPENGL | mode | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        // Wine on X11 has no window pixel formats with alpha, which SDL asks for by default.
        if (!window) {
            SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
            window = SDL_CreateWindow(recomp::game.title, 1280, 720,
                                      SDL_WINDOW_OPENGL | mode | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        }
        if (window) gl = SDL_GL_CreateContext(window);
        if (window && !gl) {
            SDL_DestroyWindow(window);
            window = nullptr;
        }
        return gl != nullptr;
    };
    if (!open_window(desktop_gl)) {
#if !defined(__linux__)
        std::fprintf(stderr, "host: no OpenGL ES 2 (%s), using desktop OpenGL\n", SDL_GetError());
        desktop_gl = true;
        SDL_GL_UnloadLibrary();
        if (!open_window(true))
#endif
        {
            std::fprintf(stderr, "host: cannot create an OpenGL window: %s\n", SDL_GetError());
            return 1;
        }
    }
    if (SDL_Surface* icon = SDL_LoadPNG((opt.game_dir + "/icon.png").c_str())) {
        SDL_SetWindowIcon(window, icon);
        SDL_DestroySurface(icon);
    }
    SDL_GL_SetSwapInterval(1);
    // Where the driver still hands out an sRGB framebuffer, its encoding is switched off.
    // Again each frame: after a fullscreen change Windows drivers may present through a
    // new surface that has it on.
    const auto disable_srgb = [disable = (desktop_gl || SDL_GL_ExtensionSupported("GL_EXT_sRGB_write_control"))
                                             ? reinterpret_cast<void (*)(unsigned)>(SDL_GL_GetProcAddress("glDisable"))
                                             : nullptr] {
        if (disable) disable(0x8DB9);  // GL_FRAMEBUFFER_SRGB
    };
    disable_srgb();
    recomp::gles::set_loader(gl_loader);
    recomp::gles::set_desktop_gl(desktop_gl);
    // Where the game's screen is on the surface (pixels from the top left): the whole
    // surface, or centred at the aspect ratio a mod holds it at.
    struct Screen {
        int x, y, w, h;
    };
    const auto screen_area = [&](int sw, int sh) {
        const bool full = SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN;
        float aspect = 0;
        for (auto& m : mods)
            if (const float a = m->screen_aspect(full); a > 0) aspect = a;
        Screen s{0, 0, sw, sh};
        if (aspect > 0 && sw > 0 && sh > 0) {
            if (float(sw) / float(sh) > aspect) s.w = std::min(sw, int(std::lround(sh * aspect)));
            else s.h = std::min(sh, int(std::lround(sw / aspect)));
            s.x = (sw - s.w) / 2;
            s.y = (sh - s.h) / 2;
        }
        return s;
    };
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    Screen screen = screen_area(w, h);
    // Surface pixels per game pixel: a mod may have the game drawn for a smaller or
    // larger screen and scaled to the surface.
    float screen_scale = 1;
    {
        recomp::java::Display d;
#ifdef __ANDROID__
        if (!android_display(d))
#endif
        {
            d.width = w;
            d.height = h;
            d.dpi = 160.0f * SDL_GetWindowDisplayScale(window);
        }
        if (screen.w != w || screen.h != h) d.width = screen.w, d.height = screen.h;
        for (auto& m : mods)
            if (const float s = m->screen_scale(d.width, d.height, d.dpi); s > 0) screen_scale = s;
        d.width = int(std::lround(d.width / screen_scale));
        d.height = int(std::lround(d.height / screen_scale));
        d.dpi /= screen_scale;
        recomp::java::set_display(d);
    }
    slingshot::AndroidInput input;
    const auto apply_screen = [&] {
        recomp::gles::set_screen_area(screen.x, h - screen.y - screen.h, screen.w, screen.h, screen_scale);
        input.set_screen_area(float(screen.x), float(screen.y), float(screen.w), float(screen.h), screen_scale);
    };
    apply_screen();

    slingshot::SdlAudio audio;
    recomp::java::set_audio_backend(&audio);
#if defined(__linux__) && !defined(__ANDROID__)
    slingshot::FontconfigProvider fonts;
#else
    slingshot::ScanFontProvider fonts;
#endif
    recomp::java::set_font_provider(&fonts);

    if (!recomp::runtime_init(opt.game_dir + "/lib/lib" + recomp::game.library + ".so")) return 1;
    recomp::run_constructors();
    if (const uint32_t onload = recomp::lib_symbol("JNI_OnLoad")) {
        jni::NativeFrame frame;
        recomp::guest_call(onload, {jni::vm(), 0});
    }

    // NativeApplication(GLSurfaceView, filesDir)
    NativeApp app;
    app.self = jni::new_global(jni::new_object(recomp::game.app_class));
    {
        jni::NativeFrame frame;
        app.call("nativeConfig", {jni::new_string(recomp::java::files_dir())});
    }
    const uint32_t orientations = app.call("nativeGetPossibleOrientations");
    app.render_thread = app.call("nativeRenderThread") & 0xff;
    std::fprintf(stderr, "host: orientations 0x%x, render thread %d\n", orientations, app.render_thread);

    // Renderer.onSurfaceChanged -> Updater.initialize / onResize
    const auto game_px = [&](int v) { return int(std::lround(v / screen_scale)); };
    app.init(game_px(screen.w), game_px(screen.h));
    app.resize(game_px(screen.w), game_px(screen.h));
    // A new surface size or fullscreen state: the screen is laid out again, and the
    // game told when its size changed.
    const auto relayout = [&] {
        SDL_GetWindowSizeInPixels(window, &w, &h);
        const Screen old = screen;
        screen = screen_area(w, h);
        apply_screen();
        if (game_px(screen.w) != game_px(old.w) || game_px(screen.h) != game_px(old.h))
            app.resize(game_px(screen.w), game_px(screen.h));
    };

    if (!opt.dump_dir.empty()) {
        jni::NativeFrame frame;
        const int n = recomp::hle::dump_encrypted_assets(opt.dump_dir);
        if (n < 0) std::fprintf(stderr, "host: cannot dump the scripts\n");
        else std::printf("wrote %d files to %s\n", n, opt.dump_dir.c_str());
        std::fflush(stdout);
        std::_Exit(n < 0 ? 1 : 0);  // the engine is in no state for an orderly shutdown
    }

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
        slingshot::ModContext ctx{input, window, w, h, SDL_GetWindowPixelDensity(window), SDL_GetTicksNS(), screen_scale,
                                  {float(screen.x), float(screen.y), float(screen.w), float(screen.h)}};
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
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
                case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN: relayout(); break;
                case SDL_EVENT_WINDOW_MINIMIZED:
                case SDL_EVENT_WILL_ENTER_BACKGROUND: app.pause(); break;
                case SDL_EVENT_WINDOW_RESTORED:
                case SDL_EVENT_DID_ENTER_FOREGROUND: app.resume(); break;
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
        disable_srgb();
        recomp::gles::clear_outside_screen(w, h);
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

}  // namespace

#ifdef _WIN32
// A GUI executable has no console: output goes to the one of the shell that
// started it, unless it is redirected.
static void attach_parent_console() {
    const bool out = GetStdHandle(STD_OUTPUT_HANDLE), err = GetStdHandle(STD_ERROR_HANDLE);
    if ((out && err) || !AttachConsole(ATTACH_PARENT_PROCESS)) return;
    if (!out) std::freopen("CONOUT$", "w", stdout);
    if (!err) std::freopen("CONOUT$", "w", stderr);
}
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
    attach_parent_console();
#endif
#ifdef __SWITCH__
    // Recompiled code needs a large stack: the game runs on a thread with 8 MiB.
    slingshot::nx::init();
    struct Args {
        int argc;
        char** argv;
        int status;
    } args{argc, argv, 1};
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 8u << 20);
    pthread_t thread;
    if (pthread_create(&thread, &attr, [](void* p) -> void* {
            auto* a = static_cast<Args*>(p);
            a->status = run(a->argc, a->argv);
            return nullptr;
        }, &args) == 0)
        pthread_join(thread, nullptr);
    pthread_attr_destroy(&attr);
    slingshot::nx::exit();
    return args.status;
#else
    return run(argc, argv);
#endif
}
