// SPDX-License-Identifier: GPL-3.0-or-later
// The part of fontconfig's API that font_provider.cpp uses, loaded at run time
// from libfontconfig.so.1: building needs no fontconfig headers. The names live
// in namespace slingshot, so they never interpose on the library's own symbols.
#pragma once

#include <dlfcn.h>

namespace slingshot {

using FcChar8 = unsigned char;
using FcChar32 = unsigned int;
using FcBool = int;
struct FcPattern;
struct FcCharSet;
struct FcConfig;
struct FcFontSet {
    int nfont;
    int sfont;
    FcPattern** fonts;
};
enum FcResult { FcResultMatch, FcResultNoMatch, FcResultTypeMismatch, FcResultNoId, FcResultOutOfMemory };
enum FcMatchKind { FcMatchPattern, FcMatchFont, FcMatchScan };

constexpr FcBool FcFalse = 0;
constexpr FcBool FcTrue = 1;
constexpr const char* FC_FAMILY = "family";
constexpr const char* FC_FILE = "file";
constexpr const char* FC_INDEX = "index";
constexpr const char* FC_SCALABLE = "scalable";
constexpr const char* FC_SLANT = "slant";
constexpr const char* FC_WEIGHT = "weight";
constexpr const char* FC_WIDTH = "width";
constexpr const char* FC_CHARSET = "charset";
constexpr int FC_SLANT_ROMAN = 0;
constexpr int FC_SLANT_ITALIC = 100;
constexpr int FC_WIDTH_CONDENSED = 75;

#define SLINGSHOT_FC_FUNCTIONS(X)                                                            \
    X(FcPattern*, FcPatternCreate, ())                                                       \
    X(void, FcPatternDestroy, (FcPattern*))                                                  \
    X(FcBool, FcPatternAddString, (FcPattern*, const char*, const FcChar8*))                 \
    X(FcBool, FcPatternAddInteger, (FcPattern*, const char*, int))                           \
    X(FcBool, FcPatternAddBool, (FcPattern*, const char*, FcBool))                           \
    X(FcResult, FcPatternGetString, (const FcPattern*, const char*, int, FcChar8**))         \
    X(FcResult, FcPatternGetInteger, (const FcPattern*, const char*, int, int*))             \
    X(FcResult, FcPatternGetCharSet, (const FcPattern*, const char*, int, FcCharSet**))      \
    X(FcBool, FcCharSetHasChar, (const FcCharSet*, FcChar32))                                \
    X(FcBool, FcConfigSubstitute, (FcConfig*, FcPattern*, FcMatchKind))                      \
    X(void, FcDefaultSubstitute, (FcPattern*))                                               \
    X(FcPattern*, FcFontMatch, (FcConfig*, FcPattern*, FcResult*))                           \
    X(FcFontSet*, FcFontSort, (FcConfig*, FcPattern*, FcBool, FcCharSet**, FcResult*))       \
    X(FcPattern*, FcFontRenderPrepare, (FcConfig*, FcPattern*, FcPattern*))                  \
    X(void, FcFontSetDestroy, (FcFontSet*))                                                  \
    X(int, FcStrCmpIgnoreCase, (const FcChar8*, const FcChar8*))                             \
    X(int, FcWeightFromOpenType, (int))                                                      \
    X(int, FcWeightToOpenType, (int))

struct FontconfigLibrary {
#define SLINGSHOT_FC_POINTER(ret, name, args) ret(*name) args = nullptr;
    SLINGSHOT_FC_FUNCTIONS(SLINGSHOT_FC_POINTER)
#undef SLINGSHOT_FC_POINTER
    bool loaded = false;

    FontconfigLibrary() {
        void* lib = dlopen("libfontconfig.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!lib) return;
        bool all = true;
#define SLINGSHOT_FC_LOAD(ret, name, args) \
    all &= (name = reinterpret_cast<ret(*) args>(dlsym(lib, #name))) != nullptr;
        SLINGSHOT_FC_FUNCTIONS(SLINGSHOT_FC_LOAD)
#undef SLINGSHOT_FC_LOAD
        loaded = all;
    }
};

inline const FontconfigLibrary& fontconfig() {
    static const FontconfigLibrary lib;
    return lib;
}

// Calls go through the loaded library; check fontconfig().loaded first.
inline FcPattern* FcPatternCreate() { return fontconfig().FcPatternCreate(); }
inline void FcPatternDestroy(FcPattern* p) { if (p) fontconfig().FcPatternDestroy(p); }
inline FcBool FcPatternAddString(FcPattern* p, const char* o, const FcChar8* s) { return fontconfig().FcPatternAddString(p, o, s); }
inline FcBool FcPatternAddInteger(FcPattern* p, const char* o, int i) { return fontconfig().FcPatternAddInteger(p, o, i); }
inline FcBool FcPatternAddBool(FcPattern* p, const char* o, FcBool b) { return fontconfig().FcPatternAddBool(p, o, b); }
inline FcResult FcPatternGetString(const FcPattern* p, const char* o, int n, FcChar8** s) { return fontconfig().FcPatternGetString(p, o, n, s); }
inline FcResult FcPatternGetInteger(const FcPattern* p, const char* o, int n, int* i) { return fontconfig().FcPatternGetInteger(p, o, n, i); }
inline FcResult FcPatternGetCharSet(const FcPattern* p, const char* o, int n, FcCharSet** c) { return fontconfig().FcPatternGetCharSet(p, o, n, c); }
inline FcBool FcCharSetHasChar(const FcCharSet* c, FcChar32 u) { return fontconfig().FcCharSetHasChar(c, u); }
inline FcBool FcConfigSubstitute(FcConfig* c, FcPattern* p, FcMatchKind k) { return fontconfig().FcConfigSubstitute(c, p, k); }
inline void FcDefaultSubstitute(FcPattern* p) { fontconfig().FcDefaultSubstitute(p); }
inline FcPattern* FcFontMatch(FcConfig* c, FcPattern* p, FcResult* r) { return fontconfig().FcFontMatch(c, p, r); }
inline FcFontSet* FcFontSort(FcConfig* c, FcPattern* p, FcBool t, FcCharSet** cs, FcResult* r) { return fontconfig().FcFontSort(c, p, t, cs, r); }
inline FcPattern* FcFontRenderPrepare(FcConfig* c, FcPattern* p, FcPattern* f) { return fontconfig().FcFontRenderPrepare(c, p, f); }
inline void FcFontSetDestroy(FcFontSet* s) { if (s) fontconfig().FcFontSetDestroy(s); }
inline int FcStrCmpIgnoreCase(const FcChar8* a, const FcChar8* b) { return fontconfig().FcStrCmpIgnoreCase(a, b); }
inline int FcWeightFromOpenType(int w) { return fontconfig().FcWeightFromOpenType(w); }
inline int FcWeightToOpenType(int w) { return fontconfig().FcWeightToOpenType(w); }

}  // namespace slingshot
