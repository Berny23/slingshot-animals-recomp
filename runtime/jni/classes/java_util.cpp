// SPDX-License-Identifier: GPL-3.0-or-later
// java.util classes the game uses through JNI: Locale, Currency, UUID and HashMap.
#include <cctype>
#include <cstdio>
#include <mutex>
#include <random>
#include <string>
#include <vector>

#include "recomp/java.hpp"
#include "recomp/jni.hpp"

namespace recomp::java {
namespace {
Locale g_locale;
std::mutex g_posted_lock;
std::vector<std::function<void()>> g_posted;
}  // namespace

void set_locale(const Locale& l) { g_locale = l; }
const Locale& locale() { return g_locale; }

Locale parse_locale(const std::string& name) {
    const std::string base = name.substr(0, name.find_first_of(".@"));
    const size_t sep = base.find_first_of("_-");
    Locale l;
    l.language = base.substr(0, sep);
    l.country = sep == std::string::npos ? "" : base.substr(sep + 1);
    for (char& ch : l.language) ch = char(std::tolower(uint8_t(ch)));
    for (char& ch : l.country) ch = char(std::toupper(uint8_t(ch)));
    const bool letters = l.language.size() >= 2 && l.language.size() <= 3 &&
                         l.language.find_first_not_of("abcdefghijklmnopqrstuvwxyz") == std::string::npos;
    if (!letters || l.language == "c" || base == "POSIX") return Locale{};
    return l;
}

void post_to_game_thread(std::function<void()> fn) {
    std::lock_guard lk(g_posted_lock);
    g_posted.push_back(std::move(fn));
}
void run_posted() {
    std::vector<std::function<void()>> work;
    {
        std::lock_guard lk(g_posted_lock);
        work.swap(g_posted);
    }
    for (auto& fn : work) fn();
}

namespace {

using namespace recomp::jni;

constexpr const char* kLocale = "java/util/Locale";

std::string field_string(Ref obj, const char* name) { return string_utf8(Ref(get_field(obj, name))); }

Ref new_locale(const Locale& l) {
    const Ref o = new_object(kLocale);
    set_field(o, "language", "Ljava/lang/String;", new_string(l.language));
    set_field(o, "country", "Ljava/lang/String;", new_string(l.country));
    return o;
}

// ---- java.util.Locale
RECOMP_JAVA_METHOD(kLocale, "getDefault", "()Ljava/util/Locale;", [](Call&) { return ref(new_locale(locale())); });
RECOMP_JAVA_METHOD(kLocale, "getLanguage", "()Ljava/lang/String;",
                   [](Call& c) { return ref(new_string(field_string(c.self, "language"))); });
RECOMP_JAVA_METHOD(kLocale, "getCountry", "()Ljava/lang/String;",
                   [](Call& c) { return ref(new_string(field_string(c.self, "country"))); });
RECOMP_JAVA_METHOD(kLocale, "toString", "()Ljava/lang/String;", [](Call& c) {
    const std::string country = field_string(c.self, "country");
    return ref(new_string(field_string(c.self, "language") + (country.empty() ? "" : "_" + country)));
});
RECOMP_JAVA_METHOD(kLocale, "toLanguageTag", "()Ljava/lang/String;", [](Call& c) {
    const std::string country = field_string(c.self, "country");
    return ref(new_string(field_string(c.self, "language") + (country.empty() ? "" : "-" + country)));
});

// ---- java.util.Currency: the ISO 4217 code of the locale's country
std::string currency_code(const std::string& country) {
    static const char* const kEuro[] = {"AT", "BE", "CY", "DE", "EE", "ES", "FI", "FR", "GR", "HR", "IE",
                                        "IT", "LT", "LU", "LV", "MT", "NL", "PT", "SI", "SK"};
    for (const char* c : kEuro)
        if (country == c) return "EUR";
    static const char* const kOther[][2] = {
        {"GB", "GBP"}, {"CH", "CHF"}, {"PL", "PLN"}, {"CZ", "CZK"}, {"SE", "SEK"}, {"NO", "NOK"},
        {"DK", "DKK"}, {"HU", "HUF"}, {"RU", "RUB"}, {"TR", "TRY"}, {"JP", "JPY"}, {"CN", "CNY"},
        {"TW", "TWD"}, {"KR", "KRW"}, {"IN", "INR"}, {"BR", "BRL"}, {"MX", "MXN"}, {"CA", "CAD"},
        {"AU", "AUD"}, {"NZ", "NZD"}, {"SA", "SAR"}, {"AE", "AED"}, {"EG", "EGP"}, {"ZA", "ZAR"},
    };
    for (const auto& p : kOther)
        if (country == p[0]) return p[1];
    return "USD";
}
RECOMP_JAVA_METHOD("java/util/Currency", "getInstance", "(Ljava/util/Locale;)Ljava/util/Currency;", [](Call& c) {
    const Ref o = new_object("java/util/Currency");
    set_field(o, "code", "Ljava/lang/String;", new_string(currency_code(field_string(c.l(0), "country"))));
    return ref(o);
});
RECOMP_JAVA_METHOD("java/util/Currency", "getCurrencyCode", "()Ljava/lang/String;",
                   [](Call& c) { return ref(new_string(field_string(c.self, "code"))); });

// ---- java.util.UUID
RECOMP_JAVA_METHOD("java/util/UUID", "randomUUID", "()Ljava/util/UUID;", [](Call&) {
    static std::mt19937_64 rng{std::random_device{}()};
    unsigned char b[16];
    for (auto& x : b) x = static_cast<unsigned char>(rng());
    b[6] = (b[6] & 0x0f) | 0x40;  // version 4
    b[8] = (b[8] & 0x3f) | 0x80;  // IETF variant
    char s[37];
    std::snprintf(s, sizeof s, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0], b[1],
                  b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    const Ref o = new_object("java/util/UUID");
    set_field(o, "value", "Ljava/lang/String;", new_string(s));
    return ref(o);
});
RECOMP_JAVA_METHOD("java/util/UUID", "toString", "()Ljava/lang/String;",
                   [](Call& c) { return ref(new_string(field_string(c.self, "value"))); });

// ---- java.util.HashMap: only built to pass analytics event parameters, which go nowhere.
RECOMP_JAVA_METHOD("java/util/HashMap", "<init>", "()V", [](Call&) { return Value(0); });
RECOMP_JAVA_METHOD("java/util/HashMap", "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                   [](Call&) { return ref(0); });

}  // namespace
}  // namespace recomp::java
