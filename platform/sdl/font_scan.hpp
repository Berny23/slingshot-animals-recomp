// SPDX-License-Identifier: GPL-3.0-or-later
// System fonts for SystemFontRenderer without fontconfig (Windows, macOS,
// Android): the font folders are read once with FreeType.
#pragma once

#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "recomp/java.hpp"

namespace slingshot {

class ScanFontProvider final : public recomp::java::FontProvider {
public:
    ScanFontProvider();
    ~ScanFontProvider() override;
    std::optional<recomp::java::FontFile> typeface(const recomp::java::FontRequest& request) override;
    std::optional<recomp::java::FontFile> fallback(const recomp::java::FontRequest& request, char32_t cp) override;

private:
    struct Face {
        std::string path, family;  // family in lower case
        int index = 0, weight = 400;
        bool italic = false, condensed = false;
        std::map<char32_t, bool> coverage;  // glyph lookups so far
    };
    void scan();
    static int distance(const Face& f, const recomp::java::FontRequest& r);
    const Face* best(const std::string& family, const recomp::java::FontRequest& r);
    bool has_glyph(Face& f, char32_t cp);

    std::mutex lock_;
    bool scanned_ = false;
    void* library_ = nullptr;  // FT_Library
    std::vector<Face> faces_;
};

}  // namespace slingshot
