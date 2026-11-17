// SPDX-License-Identifier: GPL-3.0-or-later
#include "font_scan.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <vector>

namespace slingshot {
namespace {

namespace fs = std::filesystem;
using recomp::java::FontFile;
using recomp::java::FontRequest;

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}

std::vector<fs::path> font_dirs() {
    std::vector<fs::path> dirs;
#ifdef _WIN32
    if (const char* w = std::getenv("WINDIR")) dirs.emplace_back(fs::path(w) / "Fonts");
    if (const char* l = std::getenv("LOCALAPPDATA")) dirs.emplace_back(fs::path(l) / "Microsoft" / "Windows" / "Fonts");
#elif defined(__APPLE__)
    dirs = {"/System/Library/Fonts", "/System/Library/Fonts/Supplemental", "/Library/Fonts"};
    if (const char* h = std::getenv("HOME")) dirs.emplace_back(fs::path(h) / "Library" / "Fonts");
#elif defined(__ANDROID__)
    dirs = {"/system/fonts", "/product/fonts"};
#else
    dirs = {"/usr/share/fonts", "/usr/local/share/fonts"};
    if (const char* h = std::getenv("HOME")) dirs.emplace_back(fs::path(h) / ".local" / "share" / "fonts");
#endif
    return dirs;
}

// The host's stand-ins for Android's families, the first installed one wins.
std::vector<std::string> preferred(const std::string& family) {
    if (family == "serif")
        return {"noto serif", "droid serif", "georgia", "times new roman", "times"};
    if (family == "monospace")
        return {"droid sans mono", "roboto mono", "consolas", "menlo", "courier new"};
    return {"roboto", "droid sans", "segoe ui", "helvetica neue", "helvetica", "arial"};
}

}  // namespace

ScanFontProvider::ScanFontProvider() = default;

ScanFontProvider::~ScanFontProvider() {
    if (library_) FT_Done_FreeType(static_cast<FT_Library>(library_));
}

void ScanFontProvider::scan() {
    if (scanned_) return;
    scanned_ = true;
    FT_Library lib = nullptr;
    if (FT_Init_FreeType(&lib)) return;
    library_ = lib;
    for (const auto& dir : font_dirs()) {
        std::error_code ec;
        for (fs::recursive_directory_iterator it(dir, ec), end; it != end && !ec; it.increment(ec)) {
            const std::string ext = lower(it->path().extension().string());
            if (ext != ".ttf" && ext != ".otf" && ext != ".ttc") continue;
            const std::string path = it->path().string();
            FT_Face face = nullptr;
            for (FT_Long i = 0, count = 1; i < count; ++i) {
                if (FT_New_Face(lib, path.c_str(), i, &face)) break;
                count = face->num_faces;
                if ((face->face_flags & FT_FACE_FLAG_SCALABLE) && face->family_name) {
                    Face f;
                    f.path = path;
                    f.index = int(i);
                    f.family = lower(face->family_name);
                    f.italic = face->style_flags & FT_STYLE_FLAG_ITALIC;
                    f.weight = (face->style_flags & FT_STYLE_FLAG_BOLD) ? 700 : 400;
                    const std::string style = face->style_name ? lower(face->style_name) : "";
                    if (style.find("light") != std::string::npos) f.weight = 300;
                    if (style.find("medium") != std::string::npos) f.weight = 500;
                    if (style.find("black") != std::string::npos || style.find("heavy") != std::string::npos) f.weight = 900;
                    f.condensed = style.find("condensed") != std::string::npos ||
                                  f.family.find("condensed") != std::string::npos;
                    faces_.push_back(std::move(f));
                }
                FT_Done_Face(face);
            }
        }
    }
}

// Lower is better: style distance to the request.
int ScanFontProvider::distance(const Face& f, const FontRequest& r) {
    return std::abs(f.weight - r.weight) + (f.italic != r.italic ? 1000 : 0) + (f.condensed != r.condensed ? 500 : 0);
}

const ScanFontProvider::Face* ScanFontProvider::best(const std::string& family, const FontRequest& r) {
    const Face* found = nullptr;
    for (const auto& f : faces_)
        if (f.family == family && (!found || distance(f, r) < distance(*found, r))) found = &f;
    return found;
}

std::optional<FontFile> ScanFontProvider::typeface(const FontRequest& request) {
    std::lock_guard lk(lock_);
    scan();
    for (const auto& family : preferred(request.family))
        if (const Face* f = best(family, request)) return FontFile{f->path, f->index, f->weight, f->italic};
    if (!faces_.empty()) {
        const Face& f = faces_.front();
        return FontFile{f.path, f.index, f.weight, f.italic};
    }
    return std::nullopt;
}

bool ScanFontProvider::has_glyph(Face& f, char32_t cp) {
    if (auto it = f.coverage.find(cp); it != f.coverage.end()) return it->second;
    bool has = false;
    FT_Face face = nullptr;
    if (!FT_New_Face(static_cast<FT_Library>(library_), f.path.c_str(), f.index, &face)) {
        has = FT_Get_Char_Index(face, FT_ULong(cp)) != 0;
        FT_Done_Face(face);
    }
    f.coverage[cp] = has;
    return has;
}

std::optional<FontFile> ScanFontProvider::fallback(const FontRequest& request, char32_t cp) {
    std::lock_guard lk(lock_);
    scan();
    // The preferred families first, then any font, closest style first.
    std::vector<Face*> order;
    for (const auto& family : preferred(request.family))
        for (auto& f : faces_)
            if (f.family == family) order.push_back(&f);
    std::vector<Face*> rest;
    for (auto& f : faces_) rest.push_back(&f);
    std::stable_sort(rest.begin(), rest.end(),
                     [&](const Face* a, const Face* b) { return distance(*a, request) < distance(*b, request); });
    order.insert(order.end(), rest.begin(), rest.end());
    for (Face* f : order)
        if (has_glyph(*f, cp)) return FontFile{f->path, f->index, f->weight, f->italic};
    return std::nullopt;
}

}  // namespace slingshot
