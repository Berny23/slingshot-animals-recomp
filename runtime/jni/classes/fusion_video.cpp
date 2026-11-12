// SPDX-License-Identifier: GPL-3.0-or-later
// com.rovio.fusion.VideoPlayerBridge: full-screen video (VideoView in the original).
//
// The APK ships no videos; the ones the game played came from the online
// services. Playback therefore ends right away the way a failed VideoView
// ends: onVideoEnded(handle, error = true, reason, 0, 0), posted to the game
// thread as Globals.runOnGLThread does, once per play() (the original's `k` flag).
#include <bit>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

#include "recomp/java.hpp"
#include "recomp/jni.hpp"
#include "recomp/runtime.hpp"
#include "recomp/vfs.hpp"

namespace recomp::java {
namespace {

using namespace recomp::jni;

constexpr const char* kVideo = "com/rovio/fusion/VideoPlayerBridge";

// VideoPlayer.PlaybackEndReason ordinals
constexpr uint32_t kFileNotFound = 3;
constexpr uint32_t kUnsupportedMediaType = 5;

struct Bridge {
    int64_t handle = 0;  // VideoPlayerBridge.a
    Ref self = 0;        // global reference while a callback is pending
};

std::shared_ptr<Bridge> bridge(Ref self) { return std::static_pointer_cast<Bridge>(host_data(self)); }

// A file on the device (guest path) or in the APK's assets.
bool video_exists(const std::string& path) {
    std::error_code ec;
    return !path.empty() && (std::filesystem::exists(vfs_map(path.c_str()), ec) ||
                             std::filesystem::exists(vfs_asset_root() + "/" + path, ec));
}

void end_playback(Ref self, const std::string& path) {
    auto b = bridge(self);
    if (!b || !b->handle || b->self) return;
    const uint32_t reason = video_exists(path) ? kUnsupportedMediaType : kFileNotFound;
    std::fprintf(stderr, "host: video %s not played (%s)\n", path.c_str(),
                 reason == kFileNotFound ? "file not found" : "no video playback");
    b->self = new_global(self);
    post_to_game_thread([b, reason] {
        static const uint32_t fn = native_method(kVideo, "onVideoEnded");
        if (fn) {
            NativeFrame frame;
            const uint64_t h = uint64_t(b->handle);
            // (env, this, jlong handle in r2:r3, then error, reason, position, duration on the stack)
            guest_call(fn, {env(), b->self, uint32_t(h), uint32_t(h >> 32), 1u, reason, std::bit_cast<uint32_t>(0.0f),
                            std::bit_cast<uint32_t>(0.0f)});
        }
        delete_global(b->self);
        b->self = 0;
    });
}

// VideoPlayerBridge(long handle, boolean useActivity)
RECOMP_JAVA_METHOD(kVideo, "<init>", "(JZ)V", [](Call& c) {
    auto b = std::make_shared<Bridge>();
    b->handle = c.j(0);
    set_host_data(c.self, b);
    return Value(0);
});
// playWithPath(long handle, String path, boolean ?, float startSeconds): load(path), play()
RECOMP_JAVA_METHOD(kVideo, "playWithPath", "(JLjava/lang/String;ZF)V", [](Call& c) {
    end_playback(c.self, string_utf8(c.l(1)));
    return Value(0);
});
RECOMP_JAVA_METHOD(kVideo, "isPaused", "()Z", [](Call&) { return boolean(false); });
// No player exists, so the rest does nothing (the original checks for a null VideoPlayer).
RECOMP_JAVA_METHOD(kVideo, "pause", "()V", [](Call&) { return Value(0); });
RECOMP_JAVA_METHOD(kVideo, "resume", "()V", [](Call&) { return Value(0); });
RECOMP_JAVA_METHOD(kVideo, "show", "()V", [](Call&) { return Value(0); });
RECOMP_JAVA_METHOD(kVideo, "hide", "()V", [](Call&) { return Value(0); });
RECOMP_JAVA_METHOD(kVideo, "close", "()V", [](Call&) { return Value(0); });
RECOMP_JAVA_METHOD(kVideo, "destroy", "()V", [](Call&) { return Value(0); });
RECOMP_JAVA_METHOD(kVideo, "addExtraButton", "(JLjava/lang/String;Ljava/lang/String;I)V", [](Call&) { return Value(0); });
RECOMP_JAVA_METHOD(kVideo, "addExtraLayer", "(JLjava/lang/String;FILjava/lang/String;Ljava/lang/String;F)V",
                   [](Call&) { return Value(0); });
}  // namespace
}  // namespace recomp::java
