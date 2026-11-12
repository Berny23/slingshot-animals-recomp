// SPDX-License-Identifier: GPL-3.0-or-later
// com.rovio.fusion device queries: identity, display and system features.
#include <cstdio>
#include <fstream>
#include <random>
#include <string>

#include "recomp/java.hpp"
#include "recomp/jni.hpp"
#include "recomp/vfs.hpp"

namespace recomp::java {
namespace {
Display g_display;
}
void set_display(const Display& d) { g_display = d; }
const Display& display() { return g_display; }

namespace {

using namespace recomp::jni;

// A random UUID, created once and kept with the user data (what the game
// stores as UDID when ANDROID_ID is unavailable).
std::string device_id() {
    static std::string id = [] {
        const std::string path = vfs_root() + "/device-id";
        std::string v;
        if (std::ifstream in(path); in && std::getline(in, v) && v.size() == 36) return v;
        std::random_device rd;
        unsigned char b[16];
        for (auto& x : b) x = static_cast<unsigned char>(rd());
        b[6] = (b[6] & 0x0f) | 0x40;
        b[8] = (b[8] & 0x3f) | 0x80;
        char s[37];
        std::snprintf(s, sizeof s, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0], b[1],
                      b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
        std::ofstream(path) << s << "\n";
        return std::string(s);
    }();
    return id;
}

constexpr const char* kDev = "com/rovio/fusion/DeviceInfoWrapper";

RECOMP_JAVA_METHOD("com/rovio/fusion/DeviceIDCreator", "getUniqueId", "()Ljava/lang/String;",
                   [](Call&) { return ref(new_string(device_id())); });
RECOMP_JAVA_METHOD(kDev, "getDisplayWidth", "()I", [](Call&) { return integer(display().width); });
RECOMP_JAVA_METHOD(kDev, "getDisplayHeight", "()I", [](Call&) { return integer(display().height); });
RECOMP_JAVA_METHOD(kDev, "getPPI", "()I", [](Call&) { return integer(int(display().dpi)); });
RECOMP_JAVA_METHOD(kDev, "getDisplayDensityGroup", "()I", [](Call&) {
    const float d = display().dpi;  // DisplayMetrics.densityDpi buckets
    return integer(d >= 400 ? 480 : d >= 280 ? 320 : d >= 200 ? 240 : d >= 140 ? 160 : 120);
});
// Configuration.screenLayout: SCREENLAYOUT_SIZE_LARGE | SCREENLAYOUT_LONG_YES
RECOMP_JAVA_METHOD(kDev, "getDisplayConfigurationGroup", "()I", [](Call&) { return integer(0x23); });
RECOMP_JAVA_METHOD(kDev, "hasSystemFeature", "(Ljava/lang/String;)Z", [](Call&) { return boolean(false); });

}  // namespace
}  // namespace recomp::java

// ---- android.os.Build: a generic device on the API level getAPILevel reports.
namespace recomp::java {
namespace {
using namespace recomp::jni;
struct BuildFields {
    BuildFields() {
        auto str = [](const char* cls, const char* name, const char* v) {
            define_static_field(cls, name, "Ljava/lang/String;", ref(new_global(new_string(v))));
        };
        str("android/os/Build", "MODEL", "Desktop");
        str("android/os/Build", "MANUFACTURER", "slingshot-animals-recomp");
        str("android/os/Build", "DEVICE", "desktop");
        str("android/os/Build", "BRAND", "generic");
        str("android/os/Build$VERSION", "RELEASE", "4.4.4");
        define_static_field("android/os/Build$VERSION", "SDK_INT", "I", integer(19));
    }
} g_build_fields;

// A desktop has no accelerometer; the wrapper's getters then read 0.
constexpr const char* kAccel = "com/rovio/fusion/AccelerometerWrapper";
RECOMP_JAVA_METHOD(kAccel, "start", "()V", [](Call&) { return Value(0); });
RECOMP_JAVA_METHOD(kAccel, "stop", "()V", [](Call&) { return Value(0); });
RECOMP_JAVA_METHOD(kAccel, "getX", "()F", [](Call&) { return float_(0); });
RECOMP_JAVA_METHOD(kAccel, "getY", "()F", [](Call&) { return float_(0); });
RECOMP_JAVA_METHOD(kAccel, "getZ", "()F", [](Call&) { return float_(0); });
RECOMP_JAVA_METHOD(kAccel, "getFilteredX", "()F", [](Call&) { return float_(0); });
RECOMP_JAVA_METHOD(kAccel, "getFilteredY", "()F", [](Call&) { return float_(0); });
RECOMP_JAVA_METHOD(kAccel, "getFilteredZ", "()F", [](Call&) { return float_(0); });

// App.isSilentProfile: the ringer mode is not RINGER_MODE_NORMAL. A desktop has no ringer.
RECOMP_JAVA_METHOD("com/rovio/fusion/App", "isSilentProfile", "()Z", [](Call&) { return boolean(false); });
// PackageInfo.versionName of the APK.
RECOMP_JAVA_METHOD("com/rovio/fusion/ApplicationVersion", "getApplicationVersionString", "()Ljava/lang/String;",
                   [](Call&) { return ref(new_string("8.0.3")); });
}  // namespace
}  // namespace recomp::java
