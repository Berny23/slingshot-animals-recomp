// SPDX-License-Identifier: GPL-3.0-or-later
#include "font_provider.hpp"

#include "fontconfig_dl.hpp"

#include <memory>

namespace slingshot {
namespace {

using recomp::java::FontFile;
using recomp::java::FontRequest;

using Pattern = std::unique_ptr<FcPattern, decltype(&FcPatternDestroy)>;

// The fonts Android uses for its families. They are used when installed; otherwise
// fontconfig's generic family stands in.
const char* android_font(const std::string& family) {
    if (family == "serif") return "Noto Serif";
    if (family == "monospace") return "Droid Sans Mono";
    if (family == "cursive") return "Dancing Script";
    return "Roboto";
}

Pattern request_pattern(const FontRequest& r, const char* family) {
    Pattern p(FcPatternCreate(), FcPatternDestroy);
    FcPatternAddString(p.get(), FC_FAMILY, reinterpret_cast<const FcChar8*>(family));
    FcPatternAddInteger(p.get(), FC_WEIGHT, FcWeightFromOpenType(r.weight));
    FcPatternAddInteger(p.get(), FC_SLANT, r.italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
    if (r.condensed) FcPatternAddInteger(p.get(), FC_WIDTH, FC_WIDTH_CONDENSED);
    FcPatternAddBool(p.get(), FC_SCALABLE, FcTrue);
    return p;
}

std::optional<FontFile> font_file(FcPattern* font) {
    FcChar8* file = nullptr;
    if (FcPatternGetString(font, FC_FILE, 0, &file) != FcResultMatch) return std::nullopt;
    FontFile f;
    f.path = reinterpret_cast<const char*>(file);
    int v = 0;
    if (FcPatternGetInteger(font, FC_INDEX, 0, &v) == FcResultMatch) f.index = v;
    if (FcPatternGetInteger(font, FC_WEIGHT, 0, &v) == FcResultMatch) f.weight = FcWeightToOpenType(v);
    if (FcPatternGetInteger(font, FC_SLANT, 0, &v) == FcResultMatch) f.italic = v != FC_SLANT_ROMAN;
    return f;
}

bool family_is(FcPattern* font, const char* family) {
    FcChar8* name = nullptr;
    for (int i = 0; FcPatternGetString(font, FC_FAMILY, i, &name) == FcResultMatch; ++i)
        if (!FcStrCmpIgnoreCase(name, reinterpret_cast<const FcChar8*>(family))) return true;
    return false;
}

Pattern match(FcPattern* p) {
    FcConfigSubstitute(nullptr, p, FcMatchPattern);
    FcDefaultSubstitute(p);
    FcResult result;
    return Pattern(FcFontMatch(nullptr, p, &result), FcPatternDestroy);
}

}  // namespace

std::optional<FontFile> FontconfigProvider::typeface(const FontRequest& request) {
    if (!fontconfig().loaded) return std::nullopt;
    const char* android = android_font(request.family);
    Pattern p = request_pattern(request, android);
    if (Pattern font = match(p.get()); font && family_is(font.get(), android)) return font_file(font.get());
    p = request_pattern(request, request.family.c_str());
    if (Pattern font = match(p.get())) return font_file(font.get());
    return std::nullopt;
}

std::optional<FontFile> FontconfigProvider::fallback(const FontRequest& request, char32_t cp) {
    if (!fontconfig().loaded) return std::nullopt;
    Pattern p = request_pattern(request, request.family.c_str());
    FcConfigSubstitute(nullptr, p.get(), FcMatchPattern);
    FcDefaultSubstitute(p.get());
    FcResult result;
    std::unique_ptr<FcFontSet, decltype(&FcFontSetDestroy)> set(FcFontSort(nullptr, p.get(), FcFalse, nullptr, &result),
                                                                 FcFontSetDestroy);
    if (!set) return std::nullopt;
    for (int i = 0; i < set->nfont; ++i) {
        FcCharSet* chars = nullptr;
        if (FcPatternGetCharSet(set->fonts[i], FC_CHARSET, 0, &chars) == FcResultMatch && FcCharSetHasChar(chars, cp)) {
            Pattern font(FcFontRenderPrepare(nullptr, p.get(), set->fonts[i]), FcPatternDestroy);
            return font_file(font ? font.get() : set->fonts[i]);
        }
    }
    return std::nullopt;
}

}  // namespace slingshot
