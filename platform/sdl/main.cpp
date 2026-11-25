// SPDX-License-Identifier: GPL-3.0-or-later
// SDL3 platform host: window, GLES2 context and the Android app lifecycle
// (what GLSurfaceView, NativeApplication and its Updater do on Android).
//
// Usage: slingshot-<game> [--mod <name>]... [--enable-mod <name>]... [--disable-mod <name>]... [--no-mods] [--list-mods]
//                         [--mod-option <mod>.<key>=<value>]... [--locale <ll_CC>]
//                         [--data <dir>] [--dump-scripts <dir>] [game-dir]
//   game-dir holds lib/, assets/ and icon.png from the APK (default: game/ next to the executable, else where the
//   build extracted it). The mods enabled are the build's default ones (SLINGSHOT_DEFAULT_MODS), or instead those given with
//   --mod or SLINGSHOT_MODS=name,name ("all" for every mod, "default" for those on by default);
//   --enable-mod adds one, --disable-mod takes one away, --no-mods all of them.
//   --mod-option passes a setting to a mod (see README.md for each mod's). The device locale is --locale, else
//   SLINGSHOT_LOCALE, else the host's (LC_ALL, LC_MESSAGES, LANG).
//   --data puts the saves and everything else the game writes into <dir> instead of
//   the system's place for app data.
//   --dump-scripts writes the game's scripts and configs, decrypted, into <dir>
//   and exits.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#ifdef __ANDROID__
#include <android/native_window.h>
#include <dlfcn.h>
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

#include <algorithm>
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

#ifdef _WIN32
// Whether colours drawn to the window get sRGB-encoded: a 20 % grey drawn with a shader
// reads back as about 51 without the encoding and about 124 with it. A clear does not
// tell: many drivers leave clears unencoded even where they encode what is drawn.
bool window_encodes_srgb() {
#define GLFN(type, name) const auto name = reinterpret_cast<type>(SDL_GL_GetProcAddress("gl" #name))
    GLFN(unsigned (*)(unsigned), CreateShader);
    GLFN(void (*)(unsigned, int, const char* const*, const int*), ShaderSource);
    GLFN(void (*)(unsigned), CompileShader);
    GLFN(unsigned (*)(), CreateProgram);
    GLFN(void (*)(unsigned, unsigned), AttachShader);
    GLFN(void (*)(unsigned, unsigned, const char*), BindAttribLocation);
    GLFN(void (*)(unsigned), LinkProgram);
    GLFN(void (*)(unsigned, unsigned, int*), GetProgramiv);
    GLFN(void (*)(unsigned), UseProgram);
    GLFN(void (*)(unsigned), DeleteShader);
    GLFN(void (*)(unsigned), DeleteProgram);
    GLFN(void (*)(unsigned, int, unsigned, unsigned char, int, const void*), VertexAttribPointer);
    GLFN(void (*)(unsigned), EnableVertexAttribArray);
    GLFN(void (*)(unsigned), DisableVertexAttribArray);
    GLFN(void (*)(unsigned, int, int), DrawArrays);
    GLFN(void (*)(int, int, int, int, unsigned, unsigned, void*), ReadPixels);
#undef GLFN
    if (!CreateShader || !ShaderSource || !CompileShader || !CreateProgram || !AttachShader ||
        !BindAttribLocation || !LinkProgram || !GetProgramiv || !UseProgram || !DeleteShader ||
        !DeleteProgram || !VertexAttribPointer || !EnableVertexAttribArray || !DisableVertexAttribArray ||
        !DrawArrays || !ReadPixels)
        return false;
    // No #version: GLSL ES 1.00 on OpenGL ES, GLSL 1.10 on desktop OpenGL.
    const char* const vertex = "attribute vec2 a_pos;\nvoid main() { gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
    const char* const fragment =
        "#ifdef GL_ES\nprecision mediump float;\n#endif\n"
        "void main() { gl_FragColor = vec4(0.2, 0.2, 0.2, 1.0); }\n";
    const auto compile = [&](unsigned type, const char* src) {
        const unsigned s = CreateShader(type);
        ShaderSource(s, 1, &src, nullptr);
        CompileShader(s);
        return s;
    };
    const unsigned vs = compile(0x8B31, vertex);    // GL_VERTEX_SHADER
    const unsigned fs = compile(0x8B30, fragment);  // GL_FRAGMENT_SHADER
    const unsigned program = CreateProgram();
    AttachShader(program, vs);
    AttachShader(program, fs);
    BindAttribLocation(program, 0, "a_pos");
    LinkProgram(program);
    int linked = 0;
    GetProgramiv(program, 0x8B82, &linked);  // GL_LINK_STATUS
    unsigned char px[4] = {};
    if (linked) {
        const float triangle[] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};  // covers the viewport
        UseProgram(program);
        VertexAttribPointer(0, 2, 0x1406, 0, 0, triangle);  // GL_FLOAT
        EnableVertexAttribArray(0);
        DrawArrays(0x0004, 0, 3);  // GL_TRIANGLES
        DisableVertexAttribArray(0);
        UseProgram(0);
        ReadPixels(0, 0, 1, 1, 0x1908, 0x1401, px);  // GL_RGBA, GL_UNSIGNED_BYTE
    }
    DeleteProgram(program);
    DeleteShader(vs);
    DeleteShader(fs);
    return px[0] > 90;
}
#endif

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

// Context.getExternalMediaDirs()[0] (Android/media/<package> on shared storage, which
// file managers and USB access reach without permissions), "" if there is none.
std::string android_media_dir() {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) return {};
    std::string path;
    jclass act = env->GetObjectClass(activity);
    jmethodID get_dirs = env->GetMethodID(act, "getExternalMediaDirs", "()[Ljava/io/File;");
    auto dirs = get_dirs ? static_cast<jobjectArray>(env->CallObjectMethod(activity, get_dirs)) : nullptr;
    jobject first = dirs && !env->ExceptionCheck() && env->GetArrayLength(dirs) > 0
                        ? env->GetObjectArrayElement(dirs, 0) : nullptr;
    jclass file_cls = env->FindClass("java/io/File");
    auto str = first && file_cls ? static_cast<jstring>(env->CallObjectMethod(
                                       first, env->GetMethodID(file_cls, "getAbsolutePath", "()Ljava/lang/String;")))
                                 : nullptr;
    if (str && !env->ExceptionCheck()) {
        if (const char* s = env->GetStringUTFChars(str, nullptr)) {
            path = s;
            env->ReleaseStringUTFChars(str, s);
        }
    }
    env->ExceptionClear();
    for (jobject o : {jobject(act), jobject(dirs), first, jobject(file_cls), jobject(str)})
        if (o) env->DeleteLocalRef(o);
    env->DeleteLocalRef(activity);
    return path;
}

// Whether every regular file below from has a copy of the same size below to.
bool same_files(const std::filesystem::path& from, const std::filesystem::path& to) {
    namespace fs = std::filesystem;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(from, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const fs::path copy = to / fs::relative(it->path(), from, ec);
        if (ec) return false;
        const auto size = it->file_size(ec);
        if (ec || fs::file_size(copy, ec) != size || ec) return false;
    }
    return !ec;
}

// The saves' folder: <media>/<id>. Saves of earlier versions, in the app's internal
// storage (<old>), are copied there first: into <id>.moving, which is renamed to <id>
// once every file is there, then <old> is removed. Without shared storage, or if the
// copy fails, the saves stay in <old>.
std::string android_save_dir(const std::string& old) {
    namespace fs = std::filesystem;
    const std::string media = android_media_dir();
    if (media.empty()) return old;
    const fs::path dir = fs::path(media) / recomp::game.id;
    const fs::path moving = dir.string() + ".moving";
    std::error_code ec;
    fs::create_directories(media, ec);
    if (!fs::exists(fs::path(media) / ".nomedia", ec)) SDL_SaveFile((fs::path(media) / ".nomedia").string().c_str(), "", 0);
    fs::remove_all(moving, ec);  // an interrupted copy
    if (fs::exists(dir, ec) || !fs::is_directory(old, ec) || fs::is_empty(old, ec)) {
        if (!fs::is_directory(dir, ec)) fs::create_directories(dir, ec);
        return fs::is_directory(dir, ec) ? dir.string() : old;
    }
    ec.clear();
    fs::copy(old, moving, fs::copy_options::recursive, ec);
    if (!ec && same_files(old, moving)) fs::rename(moving, dir, ec);
    else if (!ec) ec = std::make_error_code(std::errc::io_error);
    if (ec) {
        std::fprintf(stderr, "host: cannot move the saves to %s: %s\n", dir.string().c_str(), ec.message().c_str());
        fs::remove_all(moving, ec);
        return old;
    }
    fs::remove_all(old, ec);
    std::fprintf(stderr, "host: moved the saves to %s\n", dir.string().c_str());
    return dir.string();
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

// The highest refresh rate of the display at its current resolution (Display.getMode,
// getSupportedModes), 0 if unknown.
float android_max_refresh_rate() {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) return 0;
    float best = 0;
    jclass act = env->GetObjectClass(activity);
    jobject wm = env->CallObjectMethod(activity, env->GetMethodID(act, "getWindowManager", "()Landroid/view/WindowManager;"));
    jclass wm_cls = env->FindClass("android/view/WindowManager");
    jobject disp = env->CallObjectMethod(wm, env->GetMethodID(wm_cls, "getDefaultDisplay", "()Landroid/view/Display;"));
    jclass disp_cls = env->FindClass("android/view/Display");
    jclass mode_cls = env->FindClass("android/view/Display$Mode");
    jmethodID get_w = mode_cls ? env->GetMethodID(mode_cls, "getPhysicalWidth", "()I") : nullptr;
    jmethodID get_h = mode_cls ? env->GetMethodID(mode_cls, "getPhysicalHeight", "()I") : nullptr;
    jmethodID get_rate = mode_cls ? env->GetMethodID(mode_cls, "getRefreshRate", "()F") : nullptr;
    jobject cur = get_rate ? env->CallObjectMethod(disp, env->GetMethodID(disp_cls, "getMode", "()Landroid/view/Display$Mode;")) : nullptr;
    auto modes = cur ? static_cast<jobjectArray>(env->CallObjectMethod(
                           disp, env->GetMethodID(disp_cls, "getSupportedModes", "()[Landroid/view/Display$Mode;")))
                     : nullptr;
    if (modes && !env->ExceptionCheck()) {
        const int w = env->CallIntMethod(cur, get_w), h = env->CallIntMethod(cur, get_h);
        for (jsize i = 0, n = env->GetArrayLength(modes); i < n; ++i) {
            jobject m = env->GetObjectArrayElement(modes, i);
            if (env->CallIntMethod(m, get_w) == w && env->CallIntMethod(m, get_h) == h)
                best = std::max(best, env->CallFloatMethod(m, get_rate));
            env->DeleteLocalRef(m);
        }
    }
    env->ExceptionClear();
    for (jobject o : {jobject(act), wm, jobject(wm_cls), disp, jobject(disp_cls), jobject(mode_cls), cur, jobject(modes)})
        if (o) env->DeleteLocalRef(o);
    env->DeleteLocalRef(activity);
    return best;
}

// The original's GLSurfaceView runs at the display's highest rate; SDL's surface gets
// the system's default (often 60 Hz) unless it asks. ANativeWindow_setFrameRate is
// API 30; on older systems the rate stays the system's choice. Asked again for each
// surface, after refresh_rate_surface is reset when the app goes to the background.
void* refresh_rate_surface = nullptr;
void android_request_refresh_rate(SDL_Window* window) {
    using SetFrameRate = int32_t (*)(ANativeWindow*, float, int8_t);
    static const auto set_frame_rate =
        reinterpret_cast<SetFrameRate>(dlsym(RTLD_DEFAULT, "ANativeWindow_setFrameRate"));
    static const float rate = set_frame_rate ? android_max_refresh_rate() : 0;
    if (rate <= 0) return;
    void* native = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr);
    if (!native || native == refresh_rate_surface) return;
    refresh_rate_surface = native;
    set_frame_rate(static_cast<ANativeWindow*>(native), rate, ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_DEFAULT);
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
    std::string data_dir;  // --data
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

// Appends the mods of a "name,name" list; "all" stands for every mod, "default"
// for the ones on by default. With `known_only`, names of mods this game does
// not have are skipped.
void add_mods(const std::string& list, std::vector<std::string>& mods, bool known_only = false) {
    std::stringstream ss(list);
    for (std::string m; std::getline(ss, m, ',');) {
        if (m == "all" || m == "default") {
            for (const auto& info : slingshot::available_mods())
                if (m == "all" || info.on_by_default) mods.push_back(info.name);
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
    "usage: %s [--help] [--mod <name>]... [--enable-mod <name>]... [--disable-mod <name>]... [--no-mods] [--list-mods] "
    "[--mod-option <mod>.<key>=<value>]... [--locale <ll_CC>] [--data <dir>] [--dump-scripts <dir>] [game-dir]\n";

// Returns false when the program should exit right away (help, mod list).
bool parse_args(int argc, char** argv, Options& opt, int& status) {
    std::vector<std::string> chosen, added, removed;
    bool any_chosen = false, none = false;
    if (const char* env = std::getenv("SLINGSHOT_MODS")) {
        add_mods(env, chosen);
        any_chosen = true;
    }
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--mod") && i + 1 < argc) {
            add_mods(argv[++i], chosen);
            any_chosen = true;
        } else if (!std::strcmp(argv[i], "--enable-mod") && i + 1 < argc) {
            add_mods(argv[++i], added, true);
        } else if (!std::strcmp(argv[i], "--disable-mod") && i + 1 < argc) {
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
        } else if (!std::strcmp(argv[i], "--data") && i + 1 < argc) {
            opt.data_dir = argv[++i];
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
    chosen.insert(chosen.end(), added.begin(), added.end());
    // A mod this game doesn't have is off already (launchers pass one set of options to every game).
    for (const std::string& r : removed) {
        bool known = false;
        for (const auto& info : slingshot::available_mods()) known |= r == info.name;
        if (!known) std::fprintf(stderr, "host: --disable-mod %s: no such mod in this game\n", r.c_str());
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
    // On Android Android/media/<package>, reachable from file managers and over USB.
    if (char* pref = SDL_GetPrefPath(nullptr, "slingshot-animals-recomp")) {
        std::string root = std::string(pref) + recomp::game.id;
        SDL_free(pref);
#ifdef __ANDROID__
        if (opt.data_dir.empty()) root = android_save_dir(root);
#endif
        recomp::vfs_set_root(root);
    }
#endif
    if (!opt.data_dir.empty()) recomp::vfs_set_root(opt.data_dir);
    recomp::java::set_locale(recomp::java::parse_locale(opt.locale.empty() ? host_locale() : opt.locale));

    // The identifier matches the installed slingshot-<id>.desktop (the taskbar icon on Wayland).
    const std::string app_id = std::string("slingshot-") + recomp::game.id;
    SDL_SetAppMetadata(recomp::game.title, recomp::game.version, app_id.c_str());
    // Mouse and touch are separate inputs: no synthetic events in either direction.
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
#ifdef _WIN32
    // Linear output as on Android: Windows drivers otherwise may pick an sRGB framebuffer
    // that brightens every colour.
    SDL_SetHint(SDL_HINT_OPENGL_FORCE_SRGB_FRAMEBUFFER, "0");
#endif
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "host: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    // OpenGL ES 2.0 as on Android; desktop OpenGL 2.1 where there is none (macOS, some
    // Windows drivers) or with SLINGSHOT_DESKTOP_GL=1. On Windows first ANGLE (see below).
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
    bool angle = false;
#ifdef _WIN32
    // ANGLE (OpenGL ES on Direct3D 11), where libEGL.dll and libGLESv2.dll are next to the
    // executable: Direct3D presents the frames, so the GPU driver's OpenGL cannot encode
    // them as sRGB. Where it cannot start, or with SLINGSHOT_NO_ANGLE=1, the driver's
    // OpenGL; the video subsystem starts again, since SDL keeps using EGL once loaded.
    if (!desktop_gl) {
        const char* base = SDL_GetBasePath();
        const std::string dir = base ? base : "";
        const bool shipped = SDL_GetPathInfo((dir + "libEGL.dll").c_str(), nullptr) &&
                             SDL_GetPathInfo((dir + "libGLESv2.dll").c_str(), nullptr);
        if (shipped && !std::getenv("SLINGSHOT_NO_ANGLE")) {
            SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");
            angle = open_window(false);
            if (!angle) {
                std::fprintf(stderr, "host: ANGLE failed (%s), using the driver's OpenGL\n", SDL_GetError());
                SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "0");
                SDL_QuitSubSystem(SDL_INIT_VIDEO);
                if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
                    std::fprintf(stderr, "host: SDL_InitSubSystem failed: %s\n", SDL_GetError());
                    return 1;
                }
            }
        }
        // SDL itself loads libEGL.dll where the driver has no OpenGL ES: a name that does not
        // exist keeps it on the driver's OpenGL.
        if (shipped && !angle) SDL_SetHint(SDL_HINT_EGL_LIBRARY, "slingshot-no-angle.dll");
    }
#endif
    if (!angle && !open_window(desktop_gl)) {
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
    // Windows: where the driver still hands out an sRGB framebuffer, its encoding is switched
    // off: OpenGL ES can only with GL_EXT_sRGB_write_control, so where it still encodes,
    // desktop OpenGL is used. Again each frame: after a fullscreen change Windows drivers
    // may present through a new surface that has it on.
    const auto srgb_switch = [&]() -> void (*)(unsigned) {
#ifdef _WIN32
        if (desktop_gl || SDL_GL_ExtensionSupported("GL_EXT_sRGB_write_control"))
            return reinterpret_cast<void (*)(unsigned)>(SDL_GL_GetProcAddress("glDisable"));
#endif
        return nullptr;
    };
    auto disable = srgb_switch();
    const auto disable_srgb = [&] {
        if (disable) disable(0x8DB9);  // GL_FRAMEBUFFER_SRGB
    };
    disable_srgb();
#ifdef _WIN32
    if (!desktop_gl && !angle && window_encodes_srgb()) {
        std::fprintf(stderr, "host: OpenGL ES encodes colours as sRGB, using desktop OpenGL\n");
        SDL_GL_DestroyContext(gl);
        SDL_DestroyWindow(window);
        gl = nullptr;
        window = nullptr;
        SDL_GL_UnloadLibrary();
        if (open_window(true)) {
            desktop_gl = true;
        } else if (!open_window(false)) {
            std::fprintf(stderr, "host: cannot create an OpenGL window: %s\n", SDL_GetError());
            return 1;
        }
        disable = srgb_switch();
        disable_srgb();
    }
    if (window_encodes_srgb())
        std::fprintf(stderr, "host: the driver encodes colours as sRGB, they look too bright\n");
#endif
    if (const auto get_string = reinterpret_cast<const unsigned char* (*)(unsigned)>(
            SDL_GL_GetProcAddress("glGetString"))) {
        const auto text = [&](unsigned name) {
            const auto* s = get_string(name);
            return s ? reinterpret_cast<const char*>(s) : "?";
        };
        std::fprintf(stderr, "host: OpenGL %s on %s\n", text(0x1F02), text(0x1F01));  // GL_VERSION, GL_RENDERER
    }
    if (SDL_Surface* icon = SDL_LoadPNG((opt.game_dir + "/icon.png").c_str())) {
        SDL_SetWindowIcon(window, icon);
        SDL_DestroySurface(icon);
    }
    SDL_GL_SetSwapInterval(1);
    // Black until the game's first frame: Windows otherwise shows the new window
    // transparent while the game loads.
    {
        const auto clear_color =
            reinterpret_cast<void (*)(float, float, float, float)>(SDL_GL_GetProcAddress("glClearColor"));
        const auto clear = reinterpret_cast<void (*)(unsigned)>(SDL_GL_GetProcAddress("glClear"));
        if (clear_color && clear) {
            clear_color(0.0f, 0.0f, 0.0f, 1.0f);
            clear(0x4000);  // GL_COLOR_BUFFER_BIT
            clear_color(0.0f, 0.0f, 0.0f, 0.0f);
            SDL_GL_SwapWindow(window);
        }
    }
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
                case SDL_EVENT_WILL_ENTER_BACKGROUND:
#ifdef __ANDROID__
                    refresh_rate_surface = nullptr;
#endif
                    app.pause();
                    break;
                case SDL_EVENT_WINDOW_RESTORED:
                case SDL_EVENT_DID_ENTER_FOREGROUND: app.resume(); break;
                default: sdl_input.handle(e); break;
            }
        }
        for (auto& m : mods) m->on_frame(ctx);

        if (app.state != NativeApp::State::RenderReady) continue;
#ifdef __ANDROID__
        android_request_refresh_rate(window);
#endif

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
    // Unbuffered: the C runtime here buffers _IOLBF fully
    std::setvbuf(stdout, nullptr, _IONBF, 0);
#else
    // Line by line also into a file or pipe, so a log has everything up to a crash
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
#endif
#ifdef __SWITCH__
    // Recompiled code needs a large stack: the game runs on a thread with 8 MiB.
    slingshot::nx::init(recomp::game.id);
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
