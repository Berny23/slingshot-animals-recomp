// SPDX-License-Identifier: GPL-3.0-or-later
// OpenGL ES 2.0 imports, forwarded to the host's GLES2 implementation, or to
// desktop OpenGL 2.1 where the host has no GLES (gles::set_desktop_gl).
//
// The platform layer creates the context and installs a symbol loader
// (gles::set_loader). Guest memory is host memory at g_mem + address, so
// most pointers translate directly; offsets into bound buffers must stay
// offsets, which needs the buffer bindings tracked here.
#define GL_GLES_PROTOTYPES 0
#define SDL_USE_BUILTIN_OPENGL_DEFINITIONS 1
#include <SDL3/SDL_opengles2.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "recomp/gles.hpp"
#include "recomp/hle.hpp"
#include "recomp/memory.hpp"
#include "recomp/runtime.hpp"

namespace recomp::gles {
namespace {
Loader g_loader = nullptr;
bool g_desktop = false;
}  // namespace
void set_loader(Loader l) { g_loader = l; }

void set_desktop_gl(bool desktop) {
    g_desktop = desktop;
    if (!desktop || !g_loader) return;
    // Shaders write gl_PointSize and read gl_PointCoord as in GLES.
    if (auto enable = reinterpret_cast<void (*)(GLenum)>(g_loader("glEnable"))) {
        enable(0x8642);  // GL_VERTEX_PROGRAM_POINT_SIZE
        enable(0x8861);  // GL_POINT_SPRITE
    }
}
bool desktop_gl() { return g_desktop; }

namespace {
// gles::set_screen_area
struct ScreenArea {
    int x = 0, y = 0, w = 0, h = 0;
    float scale = 1;
    bool mapped() const { return scale != 1 || x || y; }
} g_area;
}  // namespace

std::string host_shader(const std::string& es) {
    if (!g_desktop) return es;
    // GLSL 1.20 knows no precision qualifiers; "#version 100" and precision statements go.
    std::string out = "#version 120\n#define lowp\n#define mediump\n#define highp\n";
    size_t pos = 0;
    while (pos < es.size()) {
        size_t end = es.find('\n', pos);
        if (end == std::string::npos) end = es.size();
        std::string line = es.substr(pos, end - pos);
        const size_t first = line.find_first_not_of(" \t");
        const std::string trimmed = first == std::string::npos ? "" : line.substr(first);
        if (trimmed.rfind("#version", 0) == 0 || trimmed.rfind("precision ", 0) == 0) line = "";
        out += line;
        out += '\n';
        pos = end + 1;
    }
    return out;
}
}  // namespace recomp::gles

namespace recomp::hle {
namespace {

using u32 = uint32_t;

template <typename T>
T* load(const char* name) {
    void* p = gles::g_loader ? gles::g_loader(name) : nullptr;
    if (!p) {
        std::fprintf(stderr, "gles: host has no %s\n", name);
        std::abort();
    }
    return reinterpret_cast<T*>(p);
}
#define GL(name, ...) static std::add_pointer_t<__VA_ARGS__> const fn = load<__VA_ARGS__>(#name)

const void* P(u32 a) { return mem::ptr(a); }
void* W(u32 a) { return mem::ptr(a); }

// Buffer bindings decide whether attribute/index pointers are offsets.
thread_local u32 t_array_buffer = 0, t_element_buffer = 0;

// ---- state
void h_glActiveTexture(u32 t) { GL(glActiveTexture, void(GLenum)); fn(t); }
void h_glBlendEquation(u32 m) { GL(glBlendEquation, void(GLenum)); fn(m); }
void h_glBlendFunc(u32 s, u32 d) { GL(glBlendFunc, void(GLenum, GLenum)); fn(s, d); }
void h_glClearColor(float r, float g, float b, float a) { GL(glClearColor, void(GLfloat, GLfloat, GLfloat, GLfloat)); fn(r, g, b, a); }
void h_glCullFace(u32 m) { GL(glCullFace, void(GLenum)); fn(m); }
void h_glDepthFunc(u32 f) { GL(glDepthFunc, void(GLenum)); fn(f); }
void h_glDepthMask(u32 f) { GL(glDepthMask, void(GLboolean)); fn(GLboolean(f)); }
bool g_scissor_test = false;
void h_glDisable(u32 c) {
    GL(glDisable, void(GLenum));
    fn(c);
    if (c == GL_SCISSOR_TEST) g_scissor_test = false;
}
void h_glEnable(u32 c) {
    GL(glEnable, void(GLenum));
    fn(c);
    if (c == GL_SCISSOR_TEST) g_scissor_test = true;
}
void h_glFinish() { GL(glFinish, void()); fn(); }
void h_glFlush() { GL(glFlush, void()); fn(); }
void h_glFrontFace(u32 m) { GL(glFrontFace, void(GLenum)); fn(m); }
u32 h_glGetError() { GL(glGetError, GLenum()); return fn(); }
void h_glPixelStorei(u32 p, int v) { GL(glPixelStorei, void(GLenum, GLint)); fn(p, v); }
// Viewport and scissor as the game set them; on the screen (framebuffer 0) they
// are mapped to gles::set_screen_area, also when the binding or the area changes later.
struct Rect {
    int x, y, w, h;
    bool set;
};
Rect g_viewport{}, g_scissor{};
bool g_on_screen = true;
bool screen_mapped() { return g_on_screen && gles::g_area.mapped(); }
// The surface rectangle of a rectangle on the game's screen.
Rect to_surface(int x, int y, int w, int h) {
    const auto& a = gles::g_area;
    const int x0 = int(std::lround(x * a.scale)), y0 = int(std::lround(y * a.scale));
    return {a.x + x0, a.y + y0, int(std::lround((x + w) * a.scale)) - x0, int(std::lround((y + h) * a.scale)) - y0, true};
}
void apply_rect(void (*fn)(GLint, GLint, GLsizei, GLsizei), const Rect& r) {
    if (!r.set) return;
    if (!screen_mapped()) return fn(r.x, r.y, r.w, r.h);
    const Rect s = to_surface(r.x, r.y, r.w, r.h);
    fn(s.x, s.y, s.w, s.h);
}
// On a mapped screen the scissor box stays inside the area, as on the device's screen.
void apply_scissor(void (*fn)(GLint, GLint, GLsizei, GLsizei)) {
    if (!g_scissor.set) return;
    if (!screen_mapped()) return fn(g_scissor.x, g_scissor.y, g_scissor.w, g_scissor.h);
    const Rect s = to_surface(g_scissor.x, g_scissor.y, g_scissor.w, g_scissor.h);
    const auto& a = gles::g_area;
    const int x0 = std::max(s.x, a.x), y0 = std::max(s.y, a.y);
    const int x1 = std::min(s.x + s.w, a.x + a.w), y1 = std::min(s.y + s.h, a.y + a.h);
    fn(x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0));
}
void apply_rects() {
    static const auto viewport = load<void(GLint, GLint, GLsizei, GLsizei)>("glViewport");
    static const auto scissor = load<void(GLint, GLint, GLsizei, GLsizei)>("glScissor");
    apply_rect(viewport, g_viewport);
    apply_scissor(scissor);
}
// On a mapped screen a clear stays inside the area.
void h_glClear(u32 m) {
    GL(glClear, void(GLbitfield));
    if (!screen_mapped() || g_scissor_test) return fn(m);
    static const auto enable = load<void(GLenum)>("glEnable");
    static const auto disable = load<void(GLenum)>("glDisable");
    static const auto scissor = load<void(GLint, GLint, GLsizei, GLsizei)>("glScissor");
    const auto& a = gles::g_area;
    scissor(a.x, a.y, a.w, a.h);
    enable(GL_SCISSOR_TEST);
    fn(m);
    disable(GL_SCISSOR_TEST);
    apply_scissor(scissor);
}
void h_glScissor(int x, int y, int w, int h) {
    GL(glScissor, void(GLint, GLint, GLsizei, GLsizei));
    g_scissor = {x, y, w, h, true};
    apply_scissor(fn);
}
void h_glViewport(int x, int y, int w, int h) {
    GL(glViewport, void(GLint, GLint, GLsizei, GLsizei));
    g_viewport = {x, y, w, h, true};
    apply_rect(fn, g_viewport);
}
void h_glGetIntegerv(u32 p, u32 out) {
    GL(glGetIntegerv, void(GLenum, GLint*));
    auto* v = static_cast<GLint*>(W(out));
    fn(p, v);
    // Viewport and scissor read back as the game set them.
    const Rect& r = p == GL_VIEWPORT ? g_viewport : g_scissor;
    if ((p == GL_VIEWPORT || p == GL_SCISSOR_BOX) && r.set && gles::g_area.mapped())
        v[0] = r.x, v[1] = r.y, v[2] = r.w, v[3] = r.h;
}
void h_glReadPixels(int x, int y, int w, int h, u32 f, u32 t, u32 p) {
    GL(glReadPixels, void(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*));
    if (!screen_mapped() || f != GL_RGBA || t != GL_UNSIGNED_BYTE || w <= 0 || h <= 0) return fn(x, y, w, h, f, t, W(p));
    // On the mapped screen: the matching surface area, resampled to w x h.
    const Rect s = to_surface(x, y, w, h);
    const int x0 = s.x, y0 = s.y, sw = std::max(1, s.w), sh = std::max(1, s.h);
    std::vector<uint32_t> src(size_t(sw) * sh);
    GLint pack = 4;
    static const auto get = load<void(GLenum, GLint*)>("glGetIntegerv");
    static const auto store = load<void(GLenum, GLint)>("glPixelStorei");
    get(GL_PACK_ALIGNMENT, &pack);
    store(GL_PACK_ALIGNMENT, 4);
    fn(x0, y0, sw, sh, f, t, src.data());
    store(GL_PACK_ALIGNMENT, pack);
    auto* out = static_cast<uint8_t*>(W(p));
    for (int row = 0; row < h; ++row) {
        const int sy = std::min(sh - 1, int((row + 0.5f) * sh / h));
        for (int col = 0; col < w; ++col) {
            const int sx = std::min(sw - 1, int((col + 0.5f) * sw / w));
            std::memcpy(out + (size_t(row) * w + col) * 4, &src[size_t(sy) * sw + sx], 4);
        }
    }
}

// glGetString returns host memory: hand out guest copies (stable per name).
u32 h_glGetString(u32 name) {
    GL(glGetString, const GLubyte*(GLenum));
    static std::mutex lock;
    static std::unordered_map<u32, u32> cache;
    std::lock_guard lk(lock);
    auto it = cache.find(name);
    if (it != cache.end()) return it->second;
    const char* s = reinterpret_cast<const char*>(fn(name));
    if (!s) return 0;
    std::string v = s;
    if (name == GL_VERSION && v.rfind("OpenGL ES", 0) != 0) v = "OpenGL ES 2.0 " + v;
    if (name == GL_SHADING_LANGUAGE_VERSION && v.rfind("OpenGL ES", 0) != 0) v = "OpenGL ES GLSL ES 1.00 " + v;
    // What the games look for, as a GLES driver names it (ETC1 is decoded here).
    if (name == GL_EXTENSIONS && gles::desktop_gl())
        v = "GL_OES_compressed_ETC1_RGB8_texture GL_OES_texture_npot GL_OES_vertex_buffer_object "
            "GL_OES_rgb8_rgba8 GL_OES_depth24 GL_OES_packed_depth_stencil";
    const u32 g = mem::static_string(v);
    cache.emplace(name, g);
    return g;
}

// ---- buffers
void h_glBindBuffer(u32 target, u32 b) {
    GL(glBindBuffer, void(GLenum, GLuint));
    if (target == GL_ARRAY_BUFFER) t_array_buffer = b;
    if (target == GL_ELEMENT_ARRAY_BUFFER) t_element_buffer = b;
    fn(target, b);
}
void h_glBufferData(u32 target, int size, u32 data, u32 usage) {
    GL(glBufferData, void(GLenum, GLsizeiptr, const void*, GLenum));
    fn(target, size, P(data), usage);
}
void h_glDeleteBuffers(int n, u32 ids) {
    GL(glDeleteBuffers, void(GLsizei, const GLuint*));
    const GLuint* p = static_cast<const GLuint*>(P(ids));
    for (int i = 0; i < n; ++i) {
        if (p[i] == t_array_buffer) t_array_buffer = 0;
        if (p[i] == t_element_buffer) t_element_buffer = 0;
    }
    fn(n, p);
}
void h_glGenBuffers(int n, u32 ids) { GL(glGenBuffers, void(GLsizei, GLuint*)); fn(n, static_cast<GLuint*>(W(ids))); }

// ---- vertex arrays and drawing
void h_glVertexAttribPointer(u32 index, int size, u32 type, u32 norm, int stride, u32 ptr) {
    GL(glVertexAttribPointer, void(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*));
    const void* p = t_array_buffer ? reinterpret_cast<const void*>(uintptr_t(ptr)) : P(ptr);
    fn(index, size, type, GLboolean(norm), stride, p);
}
void h_glEnableVertexAttribArray(u32 i) { GL(glEnableVertexAttribArray, void(GLuint)); fn(i); }
void h_glDisableVertexAttribArray(u32 i) { GL(glDisableVertexAttribArray, void(GLuint)); fn(i); }
void h_glDrawArrays(u32 mode, int first, int count) { GL(glDrawArrays, void(GLenum, GLint, GLsizei)); fn(mode, first, count); }
void h_glDrawElements(u32 mode, int count, u32 type, u32 indices) {
    GL(glDrawElements, void(GLenum, GLsizei, GLenum, const void*));
    const void* p = t_element_buffer ? reinterpret_cast<const void*>(uintptr_t(indices)) : P(indices);
    fn(mode, count, type, p);
}

// ---- textures
void h_glBindTexture(u32 t, u32 tex) { GL(glBindTexture, void(GLenum, GLuint)); fn(t, tex); }
void h_glGenTextures(int n, u32 ids) { GL(glGenTextures, void(GLsizei, GLuint*)); fn(n, static_cast<GLuint*>(W(ids))); }
void h_glDeleteTextures(int n, u32 ids) { GL(glDeleteTextures, void(GLsizei, const GLuint*)); fn(n, static_cast<const GLuint*>(P(ids))); }
void h_glTexParameteri(u32 t, u32 p, int v) { GL(glTexParameteri, void(GLenum, GLenum, GLint)); fn(t, p, v); }
void h_glTexImage2D(u32 t, int level, int ifmt, int w, int h, int border, u32 fmt, u32 type, u32 data) {
    GL(glTexImage2D, void(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*));
    fn(t, level, ifmt, w, h, border, fmt, type, P(data));
}
void h_glTexSubImage2D(u32 t, int level, int x, int y, int w, int h, u32 fmt, u32 type, u32 data) {
    GL(glTexSubImage2D, void(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*));
    fn(t, level, x, y, w, h, fmt, type, P(data));
}
// ETC1 (OES_compressed_ETC1_RGB8_texture): 4x4 blocks of 64 bits to RGB8.
std::vector<uint8_t> decode_etc1(const uint8_t* src, int w, int h) {
    static const int table[8][4] = {{2, 8, -2, -8},       {5, 17, -5, -17},     {9, 29, -9, -29},
                                    {13, 42, -13, -42},   {18, 60, -18, -60},   {24, 80, -24, -80},
                                    {33, 106, -33, -106}, {47, 183, -47, -183}};
    std::vector<uint8_t> out(size_t(w) * size_t(h) * 3);
    auto clamp = [](int v) { return uint8_t(v < 0 ? 0 : v > 255 ? 255 : v); };
    for (int by = 0; by < h; by += 4)
        for (int bx = 0; bx < w; bx += 4, src += 8) {
            const uint32_t hi = uint32_t(src[0]) << 24 | uint32_t(src[1]) << 16 | uint32_t(src[2]) << 8 | src[3];
            const uint32_t lo = uint32_t(src[4]) << 24 | uint32_t(src[5]) << 16 | uint32_t(src[6]) << 8 | src[7];
            const bool diff = hi & 2, flip = hi & 1;
            int base[2][3];
            for (int c = 0; c < 3; ++c) {
                const int shift = 24 - 8 * c;
                if (diff) {
                    const int a = (hi >> (shift + 3)) & 31;
                    int d = (hi >> shift) & 7;
                    if (d >= 4) d -= 8;
                    base[0][c] = (a << 3) | (a >> 2);
                    const int b = (a + d) & 31;
                    base[1][c] = (b << 3) | (b >> 2);
                } else {
                    const int a = (hi >> (shift + 4)) & 15, b = (hi >> shift) & 15;
                    base[0][c] = a * 17;
                    base[1][c] = b * 17;
                }
            }
            const int cw[2] = {int((hi >> 5) & 7), int((hi >> 2) & 7)};
            for (int x = 0; x < 4; ++x)
                for (int y = 0; y < 4; ++y) {
                    const int px = bx + x, py = by + y;
                    if (px >= w || py >= h) continue;
                    const int sub = flip ? (y >= 2) : (x >= 2);
                    const int i = x * 4 + y;
                    const int idx = int(((lo >> (i + 16)) & 1) << 1 | ((lo >> i) & 1));
                    // Pixel index (MSB, LSB): 0 +a, 1 +b, 2 -a, 3 -b.
                    const int mod = table[cw[sub]][idx];
                    uint8_t* o = &out[(size_t(py) * size_t(w) + size_t(px)) * 3];
                    for (int c = 0; c < 3; ++c) o[c] = clamp(base[sub][c] + mod);
                }
        }
    return out;
}

// Uploads tightly packed RGB8 rows (the unpack alignment is restored afterwards).
void upload_rgb(u32 t, int level, int w, int h, int border, const uint8_t* rgb) {
    GL(glTexImage2D, void(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*));
    static const auto store = load<void(GLenum, GLint)>("glPixelStorei");
    static const auto get = load<void(GLenum, GLint*)>("glGetIntegerv");
    GLint align = 4;
    get(GL_UNPACK_ALIGNMENT, &align);
    store(GL_UNPACK_ALIGNMENT, 1);
    fn(t, level, GL_RGB, w, h, border, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    store(GL_UNPACK_ALIGNMENT, align);
}

void h_glCompressedTexImage2D(u32 t, int level, u32 ifmt, int w, int h, int border, int size, u32 data) {
    if (gles::desktop_gl() && ifmt == 0x8D64 /*GL_ETC1_RGB8_OES*/) {
        upload_rgb(t, level, w, h, border, decode_etc1(static_cast<const uint8_t*>(P(data)), w, h).data());
        return;
    }
    GL(glCompressedTexImage2D, void(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void*));
    fn(t, level, ifmt, w, h, border, size, P(data));
}

void h_glCopyTexImage2D(u32 t, int level, u32 ifmt, int x, int y, int w, int h, int border) {
    GL(glCopyTexImage2D, void(GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLsizei, GLint));
    if (!screen_mapped()) return fn(t, level, ifmt, x, y, w, h, border);
    // On the mapped screen: the matching surface area, a texture larger or smaller
    // by the scale; coordinates relative to its size stay the same.
    const Rect s = to_surface(x, y, w, h);
    fn(t, level, ifmt, s.x, s.y, s.w, s.h, border);
}

// ---- framebuffers
void h_glBindFramebuffer(u32 t, u32 f) {
    GL(glBindFramebuffer, void(GLenum, GLuint));
    fn(t, f);
    if ((f == 0) == g_on_screen) return;
    g_on_screen = f == 0;
    if (gles::g_area.mapped()) apply_rects();
}
void h_glBindRenderbuffer(u32 t, u32 r) { GL(glBindRenderbuffer, void(GLenum, GLuint)); fn(t, r); }
void h_glGenFramebuffers(int n, u32 ids) { GL(glGenFramebuffers, void(GLsizei, GLuint*)); fn(n, static_cast<GLuint*>(W(ids))); }
void h_glGenRenderbuffers(int n, u32 ids) { GL(glGenRenderbuffers, void(GLsizei, GLuint*)); fn(n, static_cast<GLuint*>(W(ids))); }
void h_glDeleteFramebuffers(int n, u32 ids) { GL(glDeleteFramebuffers, void(GLsizei, const GLuint*)); fn(n, static_cast<const GLuint*>(P(ids))); }
void h_glDeleteRenderbuffers(int n, u32 ids) { GL(glDeleteRenderbuffers, void(GLsizei, const GLuint*)); fn(n, static_cast<const GLuint*>(P(ids))); }
void h_glFramebufferRenderbuffer(u32 t, u32 a, u32 rt, u32 r) {
    GL(glFramebufferRenderbuffer, void(GLenum, GLenum, GLenum, GLuint));
    fn(t, a, rt, r);
}
void h_glFramebufferTexture2D(u32 t, u32 a, u32 tt, u32 tex, int level) {
    GL(glFramebufferTexture2D, void(GLenum, GLenum, GLenum, GLuint, GLint));
    fn(t, a, tt, tex, level);
}
void h_glRenderbufferStorage(u32 t, u32 f, int w, int h) {
    GL(glRenderbufferStorage, void(GLenum, GLenum, GLsizei, GLsizei));
    if (gles::desktop_gl() && f == 0x8D62 /*GL_RGB565*/) f = 0x8051;  // GL_RGB8
    fn(t, f, w, h);
}
u32 h_glCheckFramebufferStatus(u32 t) { GL(glCheckFramebufferStatus, GLenum(GLenum)); return fn(t); }

// ---- shaders and programs
u32 h_glCreateShader(u32 type) { GL(glCreateShader, GLuint(GLenum)); return fn(type); }
void h_glDeleteShader(u32 s) { GL(glDeleteShader, void(GLuint)); fn(s); }
void h_glShaderSource(u32 shader, int count, u32 strings, u32 lengths) {
    GL(glShaderSource, void(GLuint, GLsizei, const GLchar* const*, const GLint*));
    std::string source;
    for (int i = 0; i < count; ++i) {
        const char* part = mem::str(ld32(strings + 4 * u32(i)));
        const int32_t len = lengths ? int32_t(ld32(lengths + 4 * u32(i))) : -1;
        source += len < 0 ? std::string(part) : std::string(part, size_t(len));
    }
    source = gles::host_shader(source);
    const GLchar* text = source.c_str();
    fn(shader, 1, &text, nullptr);
}
void h_glCompileShader(u32 s) { GL(glCompileShader, void(GLuint)); fn(s); }
void h_glGetShaderiv(u32 s, u32 p, u32 out) { GL(glGetShaderiv, void(GLuint, GLenum, GLint*)); fn(s, p, static_cast<GLint*>(W(out))); }
void h_glGetShaderInfoLog(u32 s, int n, u32 len, u32 log) {
    GL(glGetShaderInfoLog, void(GLuint, GLsizei, GLsizei*, GLchar*));
    fn(s, n, static_cast<GLsizei*>(W(len)), static_cast<GLchar*>(W(log)));
}
u32 h_glCreateProgram() { GL(glCreateProgram, GLuint()); return fn(); }
void h_glDeleteProgram(u32 p) { GL(glDeleteProgram, void(GLuint)); fn(p); }
void h_glAttachShader(u32 p, u32 s) { GL(glAttachShader, void(GLuint, GLuint)); fn(p, s); }
void h_glDetachShader(u32 p, u32 s) { GL(glDetachShader, void(GLuint, GLuint)); fn(p, s); }
void h_glLinkProgram(u32 p) { GL(glLinkProgram, void(GLuint)); fn(p); }
void h_glValidateProgram(u32 p) { GL(glValidateProgram, void(GLuint)); fn(p); }
void h_glUseProgram(u32 p) { GL(glUseProgram, void(GLuint)); fn(p); }
void h_glGetProgramiv(u32 p, u32 n, u32 out) { GL(glGetProgramiv, void(GLuint, GLenum, GLint*)); fn(p, n, static_cast<GLint*>(W(out))); }
void h_glGetProgramInfoLog(u32 p, int n, u32 len, u32 log) {
    GL(glGetProgramInfoLog, void(GLuint, GLsizei, GLsizei*, GLchar*));
    fn(p, n, static_cast<GLsizei*>(W(len)), static_cast<GLchar*>(W(log)));
}
int h_glGetAttribLocation(u32 p, u32 name) { GL(glGetAttribLocation, GLint(GLuint, const GLchar*)); return fn(p, mem::str(name)); }
void h_glBindAttribLocation(u32 p, u32 i, u32 name) {
    GL(glBindAttribLocation, void(GLuint, GLuint, const GLchar*));
    fn(p, i, mem::str(name));
}
int h_glGetUniformLocation(u32 p, u32 name) { GL(glGetUniformLocation, GLint(GLuint, const GLchar*)); return fn(p, mem::str(name)); }
void h_glGetActiveUniform(u32 p, u32 i, int n, u32 len, u32 size, u32 type, u32 name) {
    GL(glGetActiveUniform, void(GLuint, GLuint, GLsizei, GLsizei*, GLint*, GLenum*, GLchar*));
    fn(p, i, n, static_cast<GLsizei*>(W(len)), static_cast<GLint*>(W(size)), static_cast<GLenum*>(W(type)),
       static_cast<GLchar*>(W(name)));
}
void h_glGetUniformfv(u32 p, int loc, u32 out) { GL(glGetUniformfv, void(GLuint, GLint, GLfloat*)); fn(p, loc, static_cast<GLfloat*>(W(out))); }
void h_glUniform1f(int loc, float v) { GL(glUniform1f, void(GLint, GLfloat)); fn(loc, v); }
void h_glUniform1i(int loc, int v) { GL(glUniform1i, void(GLint, GLint)); fn(loc, v); }
void h_glUniform4f(int loc, float a, float b, float c, float d) { GL(glUniform4f, void(GLint, GLfloat, GLfloat, GLfloat, GLfloat)); fn(loc, a, b, c, d); }
void h_glUniformMatrix4fv(int loc, int n, u32 transpose, u32 v) {
    GL(glUniformMatrix4fv, void(GLint, GLsizei, GLboolean, const GLfloat*));
    fn(loc, n, GLboolean(transpose), static_cast<const GLfloat*>(P(v)));
}

#define REG(name) RECOMP_REGISTER_IMPORT(#name, RECOMP_HLE(h_##name))
REG(glActiveTexture); REG(glBlendEquation); REG(glBlendFunc); REG(glClear); REG(glClearColor); REG(glCullFace);
REG(glDepthFunc); REG(glDepthMask); REG(glDisable); REG(glEnable); REG(glFinish); REG(glFlush); REG(glFrontFace);
REG(glGetIntegerv); REG(glGetError); REG(glPixelStorei); REG(glScissor); REG(glViewport); REG(glReadPixels);
REG(glGetString); REG(glBindBuffer); REG(glBufferData); REG(glDeleteBuffers); REG(glGenBuffers);
REG(glVertexAttribPointer); REG(glEnableVertexAttribArray); REG(glDisableVertexAttribArray); REG(glDrawArrays);
REG(glDrawElements); REG(glBindTexture); REG(glGenTextures); REG(glDeleteTextures); REG(glTexParameteri);
REG(glTexImage2D); REG(glTexSubImage2D); REG(glCompressedTexImage2D); REG(glCopyTexImage2D);
REG(glBindFramebuffer); REG(glBindRenderbuffer); REG(glGenFramebuffers); REG(glGenRenderbuffers);
REG(glDeleteFramebuffers); REG(glDeleteRenderbuffers); REG(glFramebufferRenderbuffer); REG(glFramebufferTexture2D);
REG(glRenderbufferStorage); REG(glCheckFramebufferStatus); REG(glCreateShader); REG(glDeleteShader);
REG(glShaderSource); REG(glCompileShader); REG(glGetShaderiv); REG(glGetShaderInfoLog); REG(glCreateProgram);
REG(glDeleteProgram); REG(glAttachShader); REG(glDetachShader); REG(glLinkProgram); REG(glValidateProgram);
REG(glUseProgram); REG(glGetProgramiv); REG(glGetProgramInfoLog); REG(glGetAttribLocation);
REG(glBindAttribLocation); REG(glGetUniformLocation); REG(glGetActiveUniform); REG(glGetUniformfv);
REG(glUniform1f); REG(glUniform1i); REG(glUniform4f); REG(glUniformMatrix4fv);

}  // namespace
}  // namespace recomp::hle

namespace recomp::gles {

void set_screen_area(int x, int y, int width, int height, float scale) {
    g_area = {x, y, width, height, scale > 0 ? scale : 1};
    if (hle::g_on_screen) hle::apply_rects();
}

void clear_outside_screen(int surface_width, int surface_height) {
    const ScreenArea& a = g_area;
    if (!a.mapped() || a.w <= 0 || (a.w >= surface_width && a.h >= surface_height)) return;
    static const auto get = hle::load<void(GLenum, GLint*)>("glGetIntegerv");
    static const auto getf = hle::load<void(GLenum, GLfloat*)>("glGetFloatv");
    static const auto getb = hle::load<void(GLenum, GLboolean*)>("glGetBooleanv");
    static const auto bind = hle::load<void(GLenum, GLuint)>("glBindFramebuffer");
    static const auto enable = hle::load<void(GLenum)>("glEnable");
    static const auto disable = hle::load<void(GLenum)>("glDisable");
    static const auto mask = hle::load<void(GLboolean, GLboolean, GLboolean, GLboolean)>("glColorMask");
    static const auto color = hle::load<void(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
    static const auto clear = hle::load<void(GLbitfield)>("glClear");
    GLint fb = 0;
    GLfloat c[4];
    GLboolean m[4];
    get(GL_FRAMEBUFFER_BINDING, &fb);
    getf(GL_COLOR_CLEAR_VALUE, c);
    getb(GL_COLOR_WRITEMASK, m);
    if (fb) bind(GL_FRAMEBUFFER, 0);
    if (hle::g_scissor_test) disable(GL_SCISSOR_TEST);
    mask(1, 1, 1, 1);
    color(0, 0, 0, 1);
    clear(GL_COLOR_BUFFER_BIT);
    color(c[0], c[1], c[2], c[3]);
    mask(m[0], m[1], m[2], m[3]);
    if (hle::g_scissor_test) enable(GL_SCISSOR_TEST);
    if (fb) bind(GL_FRAMEBUFFER, GLuint(fb));
}

}  // namespace recomp::gles
