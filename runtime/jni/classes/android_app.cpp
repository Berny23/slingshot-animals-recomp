// SPDX-License-Identifier: GPL-3.0-or-later
// The Android application environment the game sees: one Activity
// (com.rovio.fusion.App) with its Context services, and com.rovio.fusion.Globals.
#include <string>

#include "recomp/game.hpp"
#include "recomp/java.hpp"
#include "recomp/jni.hpp"

namespace recomp::java {
namespace {

using namespace recomp::jni;

Ref g_activity = 0, g_assets = 0;

Ref file(const std::string& path) {
    const Ref f = new_object("java/io/File");
    const Ref s = new_string(path);
    set_field(f, "path", "Ljava/lang/String;", s);
    return f;
}

void setup_classes() {
    set_superclass("android/app/Activity", "android/content/Context");
    set_superclass("com/rovio/fusion/App", "android/app/Activity");
}

}  // namespace

Ref activity() {
    if (!g_activity) {
        setup_classes();
        g_activity = new_global(new_object("com/rovio/fusion/App"));
    }
    return g_activity;
}

std::string files_dir() { return game_data_dir() + "/files"; }
std::string cache_dir() { return game_data_dir() + "/cache"; }

namespace {

// ---- com.rovio.fusion.Globals
RECOMP_JAVA_METHOD("com/rovio/fusion/Globals", "getActivity", "()Landroid/app/Activity;",
                   [](Call&) { return ref(new_local(activity())); });
RECOMP_JAVA_METHOD("com/rovio/fusion/Globals", "getAPILevel", "()I", [](Call&) { return integer(19); });
RECOMP_JAVA_METHOD("com/rovio/fusion/Globals", "getLibraryName", "()Ljava/lang/String;",
                   [](Call&) { return ref(new_string(game.library)); });
RECOMP_JAVA_METHOD("com/rovio/fusion/Globals", "getPathToFileCacheDirectory", "()Ljava/lang/String;",
                   [](Call&) { return ref(new_string(cache_dir() + "/")); });

// ---- android.content.Context
RECOMP_JAVA_METHOD("android/content/Context", "getAssets", "()Landroid/content/res/AssetManager;", [](Call&) {
    if (!g_assets) g_assets = new_global(new_object("android/content/res/AssetManager"));
    return ref(new_local(g_assets));
});
RECOMP_JAVA_METHOD("android/content/Context", "getPackageName", "()Ljava/lang/String;",
                   [](Call&) { return ref(new_string(game.package)); });
RECOMP_JAVA_METHOD("android/content/Context", "getFilesDir", "()Ljava/io/File;",
                   [](Call&) { return ref(file(files_dir())); });
RECOMP_JAVA_METHOD("android/content/Context", "getCacheDir", "()Ljava/io/File;",
                   [](Call&) { return ref(file(cache_dir())); });
RECOMP_JAVA_METHOD("android/content/Context", "getExternalFilesDir", "(Ljava/lang/String;)Ljava/io/File;",
                   [](Call&) { return ref(file(std::string("/sdcard/Android/data/") + game.package + "/files")); });
RECOMP_JAVA_METHOD("android/content/Context", "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
                   [](Call& c) {
                       const std::string name = string_utf8(c.l(0));
                       if (name == "keyguard") return ref(new_object("android/app/KeyguardManager"));
                       if (name == "connectivity") return ref(new_object("android/net/ConnectivityManager"));
                       return ref(new_object("java/lang/Object"));
                   });

// ---- java.io.File
RECOMP_JAVA_METHOD("java/io/File", "getAbsolutePath", "()Ljava/lang/String;",
                   [](Call& c) { return ref(new_local(Ref(get_field(c.self, "path")))); });
RECOMP_JAVA_METHOD("java/io/File", "getPath", "()Ljava/lang/String;",
                   [](Call& c) { return ref(new_local(Ref(get_field(c.self, "path")))); });

}  // namespace
}  // namespace recomp::java
