// SPDX-License-Identifier: GPL-3.0-or-later
// Java side of the discontinued online services: Rovio Cloud Services
// (com.rovio.rcs), notifications and analytics. Everything answers as on a device
// without network access. Calls not listed here are logged once and return 0/null
// (see recomp/jni.hpp), e.g. SocialManagerWrapper.createSocialManagerWrapper, so
// the game runs without a social network manager and without in-app purchases.
#include <chrono>
#include <string>

#include "recomp/java.hpp"
#include "recomp/jni.hpp"

namespace recomp::java {
namespace {

using namespace recomp::jni;

constexpr const char* kUtils = "com/rovio/rcs/core/Utils";
constexpr const char* kLocalization = "com/rovio/rcs/Localization";

int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// ---- com.rovio.rcs.Localization (as on API level 19)
// deviceLocale: Locale.getDefault().toString()
RECOMP_JAVA_METHOD(kLocalization, "deviceLocale", "()Ljava/lang/String;", [](Call&) {
    const Locale& l = locale();
    return ref(new_string(l.language + (l.country.empty() ? "" : "_" + l.country)));
});
// systemLocale: "zh-cn" / "zh-tw" for the Chinese locales, otherwise the language
RECOMP_JAVA_METHOD(kLocalization, "systemLocale", "()Ljava/lang/String;", [](Call&) {
    const Locale& l = locale();
    if (l.language == "zh" && l.country == "CN") return ref(new_string("zh-cn"));
    if (l.language == "zh" && l.country == "TW") return ref(new_string("zh-tw"));
    return ref(new_string(l.language));
});

// ---- com.rovio.rcs.core.Utils
RECOMP_JAVA_METHOD(kUtils, "userAgentString", "()Ljava/lang/String;", [](Call&) {
    return ref(new_string("Mozilla/5.0 (Linux; Android 4.4.4; Desktop) AppleWebKit/537.36 (KHTML, like Gecko) "
                          "Version/4.0 Chrome/33.0.0.0 Safari/537.36"));
});
// No active network: ConnectivityManager.getActiveNetworkInfo() is null.
RECOMP_JAVA_METHOD(kUtils, "networkType", "()Ljava/lang/String;", [](Call&) { return ref(new_string("")); });
RECOMP_JAVA_METHOD(kUtils, "getCarrierName", "()Ljava/lang/String;", [](Call&) { return ref(new_string("")); });
// Not installed from a store (PackageManager.getInstallerPackageName is null).
RECOMP_JAVA_METHOD(kUtils, "getInstallSource", "()Ljava/lang/String;", [](Call&) { return ref(0); });
RECOMP_JAVA_METHOD(kUtils, "getInstallTime", "()J", [](Call&) {
    static const int64_t t = now_ms();
    return long_(t);
});
RECOMP_JAVA_METHOD(kUtils, "getUpdateTime", "()J", [](Call&) {
    static const int64_t t = now_ms();
    return long_(t);
});
RECOMP_JAVA_METHOD(kUtils, "getViewWidth", "()I", [](Call&) { return integer(display().width); });
RECOMP_JAVA_METHOD(kUtils, "getViewHeight", "()I", [](Call&) { return integer(display().height); });
RECOMP_JAVA_METHOD(kUtils, "getAndroidId", "()Ljava/lang/String;", [](Call&) { return ref(0); });
// No Google Play services: no advertising id, tracking off, no install referrer.
RECOMP_JAVA_METHOD(kUtils, "advertisingId", "()Ljava/lang/String;", [](Call&) { return ref(0); });
RECOMP_JAVA_METHOD(kUtils, "advertisingTrackingEnabled", "()Z", [](Call&) { return boolean(false); });
RECOMP_JAVA_METHOD(kUtils, "queryNewPlayReferrer", "()Z", [](Call&) { return boolean(false); });
RECOMP_JAVA_METHOD("com/rovio/rcs/ads/Utils", "test3rdPartyAdsActivities", "()V", [](Call&) { return Value(0); });

// ---- notifications: none on a desktop
RECOMP_JAVA_METHOD("com/rovio/fusion/RemoteNotificationsClientWrapper", "setEnabled", "(Z)V",
                   [](Call&) { return Value(0); });
RECOMP_JAVA_METHOD("com/rovio/fusion/LocalNotificationsWrapper", "<init>", "()V", [](Call&) { return Value(0); });
RECOMP_JAVA_METHOD("com/rovio/fusion/LocalNotificationsWrapper", "removeNotification", "(Ljava/lang/String;I)V",
                   [](Call&) { return Value(0); });

// ---- Flurry analytics: discarded
constexpr const char* kFlurry = "com/flurry/android/FlurryAgent";
RECOMP_JAVA_METHOD(kFlurry, "init", "(Landroid/content/Context;Ljava/lang/String;)V", [](Call&) { return Value(0); });
RECOMP_JAVA_METHOD(kFlurry, "onStartSession", "(Landroid/content/Context;)V", [](Call&) { return Value(0); });
RECOMP_JAVA_METHOD(kFlurry, "onEndSession", "(Landroid/content/Context;)V", [](Call&) { return Value(0); });
RECOMP_JAVA_METHOD(kFlurry, "onEvent", "(Ljava/lang/String;Ljava/util/Map;)V", [](Call&) { return Value(0); });
RECOMP_JAVA_METHOD(kFlurry, "setCaptureUncaughtExceptions", "(Z)V", [](Call&) { return Value(0); });
RECOMP_JAVA_METHOD(kFlurry, "setLocationCriteria", "(Landroid/location/Criteria;)V", [](Call&) { return Value(0); });
RECOMP_JAVA_METHOD(kFlurry, "getAgentVersion", "()I", [](Call&) { return integer(0); });
struct CriteriaFields {
    CriteriaFields() { define_static_field("android/location/Criteria", "ACCURACY_COARSE", "I", integer(2)); }
} g_criteria_fields;
RECOMP_JAVA_METHOD("android/location/Criteria", "<init>", "()V", [](Call&) { return Value(0); });
RECOMP_JAVA_METHOD("android/location/Criteria", "setAccuracy", "(I)V", [](Call&) { return Value(0); });

}  // namespace
}  // namespace recomp::java
