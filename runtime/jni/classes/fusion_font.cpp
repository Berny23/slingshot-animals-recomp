// SPDX-License-Identifier: GPL-3.0-or-later
// com.rovio.fusion.SystemFontRenderer: text in the device's system font, drawn into
// an ARGB bitmap that the engine uploads as a texture.
//
// The original wraps two android.graphics.Paints: `a` fills the text, `b`
// (FILL_AND_STROKE, stroke width 2 * outline) draws the outline under it.
// Here Paint, Canvas.drawText and Bitmap.getPixels are rebuilt on FreeType
// and HarfBuzz the way Skia/Minikin do them: font metrics from the primary
// typeface, shaping per run, characters it lacks from fallback fonts, glyph
// origins on whole pixels (no SUBPIXEL_TEXT_FLAG) and synthetic bold/italic.
// The platform's java::FontProvider finds the font files.
#include <hb-ft.h>
#include <hb.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H
#include FT_STROKER_H
#include FT_TRUETYPE_TABLES_H

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "recomp/java.hpp"
#include "recomp/jni.hpp"
#include "recomp/memory.hpp"
#include "recomp/runtime.hpp"

namespace recomp::java {
namespace {

using namespace recomp::jni;

constexpr const char* kFont = "com/rovio/fusion/SystemFontRenderer";

FontProvider* g_provider = nullptr;

bool trace() {
    static const bool on = trace_enabled("fonts");
    return on;
}

// ---- font files

struct Face {
    FT_Face ft = nullptr;
    hb_font_t* hb = nullptr;
    FontFile file;
};

FT_Library library() {
    static FT_Library lib = [] {
        FT_Library l = nullptr;
        if (FT_Init_FreeType(&l)) std::fprintf(stderr, "fonts: cannot initialise FreeType\n");
        return l;
    }();
    return lib;
}

// Faces stay open for the whole run (a handful of system fonts).
Face* open_face(const FontFile& file) {
    static std::map<std::pair<std::string, int>, std::unique_ptr<Face>> faces;
    auto& slot = faces[{file.path, file.index}];
    if (slot) return slot->ft ? slot.get() : nullptr;
    slot = std::make_unique<Face>();
    if (!library() || FT_New_Face(library(), file.path.c_str(), file.index, &slot->ft) || !FT_IS_SCALABLE(slot->ft)) {
        std::fprintf(stderr, "fonts: cannot use %s\n", file.path.c_str());
        if (slot->ft) FT_Done_Face(slot->ft);
        slot->ft = nullptr;
        return nullptr;
    }
    slot->file = file;
    slot->hb = hb_ft_font_create_referenced(slot->ft);
    // Unhinted advances, as Minikin lays out text with fractional advances.
    hb_ft_font_set_load_flags(slot->hb, FT_LOAD_NO_HINTING);
    return slot.get();
}

// Typeface.create(String familyName, int style). Since Android 5 the name is
// looked up in the system font map (fonts.xml, case-sensitive); unknown names
// such as the game's "Sans_Serif" give the default typeface.
FontRequest typeface_request(const std::string& name, int style) {
    struct Family {
        const char* name;
        const char* family;
        int weight;
        bool condensed;
    };
    static const Family kFamilies[] = {
        {"sans-serif", "sans-serif", 400, false},
        {"sans-serif-thin", "sans-serif", 100, false},
        {"sans-serif-light", "sans-serif", 300, false},
        {"sans-serif-medium", "sans-serif", 500, false},
        {"sans-serif-black", "sans-serif", 900, false},
        {"sans-serif-condensed", "sans-serif", 400, true},
        {"sans-serif-condensed-light", "sans-serif", 300, true},
        {"arial", "sans-serif", 400, false},
        {"helvetica", "sans-serif", 400, false},
        {"tahoma", "sans-serif", 400, false},
        {"verdana", "sans-serif", 400, false},
        {"serif", "serif", 400, false},
        {"times", "serif", 400, false},
        {"times new roman", "serif", 400, false},
        {"palatino", "serif", 400, false},
        {"georgia", "serif", 400, false},
        {"baskerville", "serif", 400, false},
        {"goudy", "serif", 400, false},
        {"fantasy", "serif", 400, false},
        {"ITC Stone Serif", "serif", 400, false},
        {"monospace", "monospace", 400, false},
        {"serif-monospace", "monospace", 400, false},
        {"courier", "monospace", 400, false},
        {"courier new", "monospace", 400, false},
        {"monaco", "monospace", 400, false},
        {"casual", "cursive", 400, false},
        {"cursive", "cursive", 400, false},
    };
    FontRequest r;
    for (const auto& f : kFamilies)
        if (name == f.name) {
            r.family = f.family;
            r.weight = f.weight;
            r.condensed = f.condensed;
        }
    if (style & 1) r.weight = std::min(r.weight + 300, 1000);  // Typeface.BOLD
    r.italic = style & 2;                                       // Typeface.ITALIC
    return r;
}

// ---- glyphs

struct GlyphBitmap {
    int left = 0, top = 0;  // relative to the glyph origin, y down
    int width = 0, rows = 0;
    std::vector<uint8_t> coverage;
};

struct Placed {
    Face* face;
    uint32_t glyph;
    int x, y;  // glyph origin on the baseline (y down)
};

struct Rect {
    int left = 0, top = 0, right = 0, bottom = 0;
};

struct Metrics {  // Paint.FontMetrics (y down: top and ascent are negative)
    float top = 0, ascent = 0, descent = 0, bottom = 0, leading = 0;
};

// One android.graphics.Paint as SystemFontRenderer configures it.
struct Paint {
    float size = 0;
    uint32_t color = 0xff000000;
    float stroke_width = 0;  // FILL_AND_STROKE when > 0
    std::map<std::pair<Face*, uint32_t>, GlyphBitmap> cache;
};

struct Renderer {
    FontRequest request;
    Face* primary = nullptr;
    Paint fill, outline;  // the original's `a` and `b`
    int outline_px = 0;   // SystemFontRenderer.c
    std::unordered_map<char32_t, Face*> fallbacks;
};

void set_size(Face* f, float size) {
    FT_Set_Char_Size(f->ft, 0, FT_F26Dot6(std::lround(size * 64)), 72, 72);
    hb_ft_font_changed(f->hb);
}

bool fake_bold(const Renderer& r, const Face* f) { return r.request.weight >= 600 && r.request.weight - f->file.weight >= 200; }
bool fake_italic(const Renderer& r, const Face* f) { return r.request.italic && !f->file.italic; }

const GlyphBitmap& glyph_bitmap(Renderer& r, Paint& p, Face* f, uint32_t glyph) {
    auto [it, fresh] = p.cache.try_emplace({f, glyph});
    GlyphBitmap& out = it->second;
    if (!fresh) return out;
    set_size(f, p.size);
    if (FT_Load_Glyph(f->ft, glyph, FT_LOAD_NO_BITMAP | FT_LOAD_TARGET_LIGHT)) return out;
    FT_GlyphSlot slot = f->ft->glyph;
    if (slot->format != FT_GLYPH_FORMAT_OUTLINE) return out;
    if (fake_bold(r, f)) {  // Skia: outline widened by 1/24 em
        const FT_Pos strength = FT_MulFix(f->ft->units_per_EM, f->ft->size->metrics.y_scale) / 24;
        FT_Outline_Embolden(&slot->outline, strength);
    }
    if (fake_italic(r, f)) {  // Skia: skew x by -0.25 (y down)
        FT_Matrix skew{0x10000, 0x4000, 0, 0x10000};
        FT_Outline_Transform(&slot->outline, &skew);
    }
    FT_Glyph g = nullptr;
    if (FT_Get_Glyph(slot, &g)) return out;
    if (p.stroke_width > 0) {
        // Paint defaults: miter join (limit 4), butt cap. Fill plus stroke is the
        // area inside the outer border.
        FT_Stroker stroker = nullptr;
        if (!FT_Stroker_New(library(), &stroker)) {
            FT_Stroker_Set(stroker, FT_Fixed(std::lround(p.stroke_width / 2 * 64)), FT_STROKER_LINECAP_BUTT,
                           FT_STROKER_LINEJOIN_MITER_VARIABLE, 4 << 16);
            FT_Glyph_StrokeBorder(&g, stroker, false, true);
            FT_Stroker_Done(stroker);
        }
    }
    if (!FT_Glyph_To_Bitmap(&g, FT_RENDER_MODE_NORMAL, nullptr, true)) {
        const auto* bg = reinterpret_cast<FT_BitmapGlyph>(g);
        const FT_Bitmap& bm = bg->bitmap;
        out.left = bg->left;
        out.top = -bg->top;
        out.width = int(bm.width);
        out.rows = int(bm.rows);
        out.coverage.resize(size_t(out.width) * size_t(out.rows));
        for (int y = 0; y < out.rows; ++y)
            std::memcpy(out.coverage.data() + size_t(y) * size_t(out.width), bm.buffer + ptrdiff_t(y) * bm.pitch,
                        size_t(out.width));
    }
    FT_Done_Glyph(g);
    return out;
}

// ---- layout

std::u32string decode_utf8(const std::string& s) {
    std::u32string out;
    for (size_t i = 0; i < s.size();) {
        const auto c = uint8_t(s[i]);
        const int n = c < 0x80 ? 1 : c >> 5 == 6 ? 2 : c >> 4 == 14 ? 3 : c >> 3 == 30 ? 4 : 0;
        if (!n || i + size_t(n) > s.size()) {
            out += U'�';
            ++i;
            continue;
        }
        char32_t cp = n == 1 ? c : c & (0x7f >> n);
        for (int k = 1; k < n; ++k) cp = cp << 6 | (uint8_t(s[i + size_t(k)]) & 0x3f);
        out += cp;
        i += size_t(n);
    }
    return out;
}

bool has_glyph(Face* f, char32_t cp) { return FT_Get_Char_Index(f->ft, cp) != 0; }

// Characters that stay with the font of the text around them.
bool joins_previous(char32_t cp) {
    const auto cat = hb_unicode_general_category(hb_unicode_funcs_get_default(), cp);
    return cat == HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK || cat == HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK ||
           cat == HB_UNICODE_GENERAL_CATEGORY_ENCLOSING_MARK || cat == HB_UNICODE_GENERAL_CATEGORY_FORMAT ||
           (cp >= 0xfe00 && cp <= 0xfe0f) || (cp >= 0xe0100 && cp <= 0xe01ef);
}

Face* font_for(Renderer& r, char32_t cp) {
    if (has_glyph(r.primary, cp)) return r.primary;
    auto [it, fresh] = r.fallbacks.try_emplace(cp, nullptr);
    if (fresh && g_provider) {
        if (auto file = g_provider->fallback(r.request, cp))
            if (Face* f = open_face(*file); f && has_glyph(f, cp)) it->second = f;
        if (trace())
            std::fprintf(stderr, "fonts: U+%04X from %s\n", unsigned(cp),
                         it->second ? it->second->file.path.c_str() : "nowhere (.notdef)");
    }
    return it->second ? it->second : r.primary;
}

// The glyphs of `text` with the pen starting at (0, 0) on the baseline.
std::vector<Placed> layout(Renderer& r, const std::string& utf8) {
    std::vector<Placed> out;
    if (!r.primary) return out;
    const std::u32string text = decode_utf8(utf8);
    double pen = 0;  // pixels
    for (size_t start = 0; start < text.size();) {
        Face* face = font_for(r, text[start]);
        size_t end = start + 1;
        while (end < text.size() && (joins_previous(text[end]) || font_for(r, text[end]) == face)) ++end;
        set_size(face, r.fill.size);
        hb_buffer_t* buf = hb_buffer_create();
        hb_buffer_add_utf32(buf, reinterpret_cast<const uint32_t*>(text.data()), int(text.size()), unsigned(start),
                            int(end - start));
        hb_buffer_guess_segment_properties(buf);
        hb_shape(face->hb, buf, nullptr, 0);
        unsigned n = 0;
        const hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buf, &n);
        const hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, &n);
        for (unsigned i = 0; i < n; ++i) {
            out.push_back({face, info[i].codepoint, int(std::lround(pen + pos[i].x_offset / 64.0)),
                           -int(std::lround(pos[i].y_offset / 64.0))});
            pen += pos[i].x_advance / 64.0;
        }
        hb_buffer_destroy(buf);
        start = end;
    }
    return out;
}

// Paint.getTextBounds: the ink of the text, relative to the origin.
Rect text_bounds(Renderer& r, const std::vector<Placed>& glyphs) {
    Rect b;
    bool any = false;
    for (const auto& g : glyphs) {
        const GlyphBitmap& bm = glyph_bitmap(r, r.fill, g.face, g.glyph);
        if (!bm.width || !bm.rows) continue;
        const Rect gr{g.x + bm.left, g.y + bm.top, g.x + bm.left + bm.width, g.y + bm.top + bm.rows};
        if (!any) b = gr;
        b = {std::min(b.left, gr.left), std::min(b.top, gr.top), std::max(b.right, gr.right),
             std::max(b.bottom, gr.bottom)};
        any = true;
    }
    return b;
}

// Skia's FreeType metrics of the primary typeface (hhea, or OS/2 typo metrics
// when USE_TYPO_METRICS is set).
Metrics font_metrics(const Renderer& r) {
    Metrics m;
    if (!r.primary) return m;
    const FT_Face f = r.primary->ft;
    const float scale = r.fill.size / float(f->units_per_EM);
    float ascender = f->ascender, descender = f->descender;
    float gap = float(f->height - (f->ascender - f->descender));
    if (const auto* os2 = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(f, FT_SFNT_OS2));
        os2 && os2->version != 0xffff && (os2->fsSelection & (1 << 7))) {
        ascender = os2->sTypoAscender;
        descender = os2->sTypoDescender;
        gap = os2->sTypoLineGap;
    }
    m.ascent = -ascender * scale;
    m.descent = -descender * scale;
    m.leading = gap * scale;
    m.top = -float(f->bbox.yMax) * scale;
    m.bottom = -float(f->bbox.yMin) * scale;
    return m;
}

// Canvas.drawText(text, x, y, paint) into premultiplied RGBA floats.
void draw_text(Renderer& r, Paint& p, const std::vector<Placed>& glyphs, int x, int y, std::vector<float>& px, int w,
               int h) {
    const float a = float(p.color >> 24) / 255.0f;
    const float rgb[3] = {float(p.color >> 16 & 0xff) / 255.0f, float(p.color >> 8 & 0xff) / 255.0f,
                          float(p.color & 0xff) / 255.0f};
    for (const auto& g : glyphs) {
        const GlyphBitmap& bm = glyph_bitmap(r, p, g.face, g.glyph);
        const int ox = x + g.x + bm.left, oy = y + g.y + bm.top;
        for (int row = 0; row < bm.rows; ++row) {
            const int py = oy + row;
            if (py < 0 || py >= h) continue;
            for (int col = 0; col < bm.width; ++col) {
                const int pxx = ox + col;
                if (pxx < 0 || pxx >= w) continue;
                const float sa = a * float(bm.coverage[size_t(row) * size_t(bm.width) + size_t(col)]) / 255.0f;
                if (sa <= 0) continue;
                float* d = &px[(size_t(py) * size_t(w) + size_t(pxx)) * 4];
                for (int k = 0; k < 3; ++k) d[k] = rgb[k] * sa + d[k] * (1 - sa);
                d[3] = sa + d[3] * (1 - sa);
            }
        }
    }
}

std::shared_ptr<Renderer> renderer(Ref self) { return std::static_pointer_cast<Renderer>(host_data(self)); }

int width_of(Renderer& r, const std::string& s) {
    const Rect b = text_bounds(r, layout(r, s));
    return b.right - b.left;
}
int height_of(const Renderer& r) {
    const Metrics m = font_metrics(r);
    return int(m.bottom - m.top);
}

// SystemFontRenderer(String fontName, int size, int color, int outlineSize, int outlineColor, int style)
RECOMP_JAVA_METHOD(kFont, "<init>", "(Ljava/lang/String;IIIII)V", [](Call& c) {
    auto r = std::make_shared<Renderer>();
    const std::string name = string_utf8(c.l(0));
    r->request = typeface_request(name, c.i(5));
    r->fill.size = r->outline.size = float(c.i(1));
    r->fill.color = uint32_t(c.i(2));
    r->outline.color = uint32_t(c.i(4));
    r->outline_px = std::max(c.i(3), 0);
    r->outline.stroke_width = float(r->outline_px * 2);
    if (g_provider)
        if (auto file = g_provider->typeface(r->request)) r->primary = open_face(*file);
    if (!r->primary) std::fprintf(stderr, "fonts: no system font for \"%s\"; text stays empty\n", name.c_str());
    if (trace())
        std::fprintf(stderr, "fonts: SystemFontRenderer(\"%s\", %d px, outline %d, style %d) -> %s\n", name.c_str(),
                     c.i(1), r->outline_px, c.i(5), r->primary ? r->primary->file.path.c_str() : "none");
    set_host_data(c.self, r);
    return Value(0);
});

RECOMP_JAVA_METHOD(kFont, "drawString", "(Ljava/lang/String;)[I", [](Call& c) {
    auto r = renderer(c.self);
    if (!r) return ref(new_array('I', 0));
    const std::string text = string_utf8(c.l(0));
    const auto glyphs = layout(*r, text);
    const Rect b = text_bounds(*r, glyphs);
    const int pad = r->outline_px * 2;
    const int w = b.right - b.left + pad;
    const int h = height_of(*r) + pad;
    const Ref array = new_array('I', uint32_t(std::max(w * h, 0)));
    if (w <= 0 || h <= 0) return ref(array);
    // Text origin as the original computes it: x = -bounds.left, y = -(int)fontMetrics.top,
    // both moved in by the outline width.
    const int x = -b.left + r->outline_px;
    const int y = -int(font_metrics(*r).top) + r->outline_px;
    std::vector<float> px(size_t(w) * size_t(h) * 4, 0.0f);
    if (r->outline_px > 0) draw_text(*r, r->outline, glyphs, x, y, px, w, h);
    draw_text(*r, r->fill, glyphs, x, y, px, w, h);
    // Bitmap.getPixels: unpremultiplied ARGB.
    auto* out = reinterpret_cast<uint32_t*>(g_mem + array_data(array));
    for (size_t i = 0; i < size_t(w) * size_t(h); ++i) {
        const float* p = &px[i * 4];
        const uint32_t a = uint32_t(std::lround(std::clamp(p[3], 0.0f, 1.0f) * 255));
        uint32_t v = a << 24;
        if (a)
            for (int k = 0; k < 3; ++k)
                v |= uint32_t(std::lround(std::clamp(p[k] / p[3], 0.0f, 1.0f) * 255)) << (16 - 8 * k);
        out[i] = v;
    }
    if (trace()) std::fprintf(stderr, "fonts: drawString(\"%s\") %dx%d\n", text.c_str(), w, h);
    return ref(array);
});

RECOMP_JAVA_METHOD(kFont, "getAscender", "()I", [](Call& c) {
    auto r = renderer(c.self);
    return integer(r ? -int(font_metrics(*r).ascent) : 0);
});
RECOMP_JAVA_METHOD(kFont, "getDescender", "()I", [](Call& c) {
    auto r = renderer(c.self);
    return integer(r ? int(font_metrics(*r).descent) : 0);
});
RECOMP_JAVA_METHOD(kFont, "getLeading", "()I", [](Call& c) {
    auto r = renderer(c.self);
    return integer(r ? int(font_metrics(*r).leading) : 0);
});
RECOMP_JAVA_METHOD(kFont, "getTop", "(Ljava/lang/String;)I", [](Call& c) {
    auto r = renderer(c.self);
    return integer(r ? int(font_metrics(*r).top) : 0);
});
RECOMP_JAVA_METHOD(kFont, "getHeight", "(Ljava/lang/String;)I", [](Call& c) {
    auto r = renderer(c.self);
    return integer(r ? height_of(*r) : 0);
});
RECOMP_JAVA_METHOD(kFont, "getWidth", "(Ljava/lang/String;)I", [](Call& c) {
    auto r = renderer(c.self);
    return integer(r ? width_of(*r, string_utf8(c.l(0))) : 0);
});
RECOMP_JAVA_METHOD(kFont, "getLeft", "(Ljava/lang/String;)I", [](Call& c) {
    auto r = renderer(c.self);
    return integer(r ? text_bounds(*r, layout(*r, string_utf8(c.l(0)))).left : 0);
});

}  // namespace

void set_font_provider(FontProvider* provider) { g_provider = provider; }

}  // namespace recomp::java
