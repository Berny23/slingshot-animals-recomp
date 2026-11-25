// SPDX-License-Identifier: GPL-3.0-or-later
#include "overlay.hpp"

#include <SDL3/SDL.h>
#define GL_GLES_PROTOTYPES 0
#include <SDL3/SDL_opengles2_khrplatform.h>
#include <SDL3/SDL_opengles2_gl2platform.h>
#include <SDL3/SDL_opengles2_gl2.h>

#include <cstdio>
#include <string>

#include "recomp/gles.hpp"

namespace slingshot {
namespace {

struct Gl {
#define GLFN(type, name) type name = reinterpret_cast<type>(SDL_GL_GetProcAddress("gl" #name))
    GLFN(PFNGLGETINTEGERVPROC, GetIntegerv);
    GLFN(PFNGLGETBOOLEANVPROC, GetBooleanv);
    GLFN(PFNGLISENABLEDPROC, IsEnabled);
    GLFN(PFNGLENABLEPROC, Enable);
    GLFN(PFNGLDISABLEPROC, Disable);
    GLFN(PFNGLBLENDFUNCSEPARATEPROC, BlendFuncSeparate);
    GLFN(PFNGLBLENDEQUATIONSEPARATEPROC, BlendEquationSeparate);
    GLFN(PFNGLCOLORMASKPROC, ColorMask);
    GLFN(PFNGLVIEWPORTPROC, Viewport);
    GLFN(PFNGLCREATESHADERPROC, CreateShader);
    GLFN(PFNGLSHADERSOURCEPROC, ShaderSource);
    GLFN(PFNGLCOMPILESHADERPROC, CompileShader);
    GLFN(PFNGLCREATEPROGRAMPROC, CreateProgram);
    GLFN(PFNGLATTACHSHADERPROC, AttachShader);
    GLFN(PFNGLBINDATTRIBLOCATIONPROC, BindAttribLocation);
    GLFN(PFNGLLINKPROGRAMPROC, LinkProgram);
    GLFN(PFNGLGETPROGRAMIVPROC, GetProgramiv);
    GLFN(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation);
    GLFN(PFNGLUSEPROGRAMPROC, UseProgram);
    GLFN(PFNGLUNIFORM4FPROC, Uniform4f);
    GLFN(PFNGLUNIFORM2FPROC, Uniform2f);
    GLFN(PFNGLBINDBUFFERPROC, BindBuffer);
    GLFN(PFNGLGETVERTEXATTRIBIVPROC, GetVertexAttribiv);
    GLFN(PFNGLGETVERTEXATTRIBPOINTERVPROC, GetVertexAttribPointerv);
    GLFN(PFNGLVERTEXATTRIBPOINTERPROC, VertexAttribPointer);
    GLFN(PFNGLENABLEVERTEXATTRIBARRAYPROC, EnableVertexAttribArray);
    GLFN(PFNGLDISABLEVERTEXATTRIBARRAYPROC, DisableVertexAttribArray);
    GLFN(PFNGLDRAWARRAYSPROC, DrawArrays);
    GLFN(PFNGLGENTEXTURESPROC, GenTextures);
    GLFN(PFNGLBINDTEXTUREPROC, BindTexture);
    GLFN(PFNGLTEXIMAGE2DPROC, TexImage2D);
    GLFN(PFNGLTEXPARAMETERIPROC, TexParameteri);
    GLFN(PFNGLACTIVETEXTUREPROC, ActiveTexture);
    GLFN(PFNGLUNIFORM1IPROC, Uniform1i);
    GLFN(PFNGLPIXELSTOREIPROC, PixelStorei);
#undef GLFN
};

const char* const kVertex =
    "attribute vec2 a_pos;\n"
    "uniform vec2 u_size;\n"
    "varying vec2 v_pos;\n"
    "void main() {\n"
    "    v_pos = a_pos;\n"
    "    gl_Position = vec4(a_pos / u_size * vec2(2.0, -2.0) + vec2(-1.0, 1.0), 0.0, 1.0);\n"
    "}\n";
const char* const kFragment =
    "precision mediump float;\n"
    "uniform vec4 u_color;\n"
    "uniform vec4 u_ring;\n"  // center x, y, outer, inner radius
    "varying vec2 v_pos;\n"
    "void main() {\n"
    "    float d = distance(v_pos, u_ring.xy);\n"
    "    float a = clamp(u_ring.z - d, 0.0, 1.0) * clamp(d - u_ring.w + 1.0, 0.0, 1.0);\n"
    "    gl_FragColor = vec4(u_color.rgb, u_color.a * a);\n"
    "}\n";

const char* const kImageVertex =
    "attribute vec2 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "uniform vec2 u_size;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    v_uv = a_uv;\n"
    "    gl_Position = vec4(a_pos / u_size * vec2(2.0, -2.0) + vec2(-1.0, 1.0), 0.0, 1.0);\n"
    "}\n";
const char* const kImageFragment =
    "precision mediump float;\n"
    "uniform sampler2D u_texture;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    gl_FragColor = vec4(texture2D(u_texture, v_uv).rgb, 1.0);\n"
    "}\n";

GLuint link(const Gl& gl, const char* vertex, const char* fragment, const char* attr1) {
    auto compile = [&](GLenum type, const char* src) {
        const GLuint s = gl.CreateShader(type);
        const std::string text = recomp::gles::host_shader(src);
        const char* p = text.c_str();
        gl.ShaderSource(s, 1, &p, nullptr);
        gl.CompileShader(s);
        return s;
    };
    const GLuint program = gl.CreateProgram();
    gl.AttachShader(program, compile(GL_VERTEX_SHADER, vertex));
    gl.AttachShader(program, compile(GL_FRAGMENT_SHADER, fragment));
    gl.BindAttribLocation(program, 0, "a_pos");
    if (attr1) gl.BindAttribLocation(program, 1, attr1);
    gl.LinkProgram(program);
    GLint ok = 0;
    gl.GetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) std::fprintf(stderr, "host: overlay shader failed to link\n");
    return program;
}

// Saves the state Overlay::render touches and restores it on destruction.
struct SavedState {
    const Gl& gl;
    GLint program, array_buffer, viewport[4];
    GLint blend_src_rgb, blend_dst_rgb, blend_src_a, blend_dst_a, blend_eq_rgb, blend_eq_a;
    GLboolean blend, depth, stencil, scissor, cull, color_mask[4];
    GLint attr_enabled, attr_size, attr_type, attr_norm, attr_stride, attr_buffer;
    void* attr_pointer = nullptr;
    GLint attr1_enabled, attr1_size, attr1_type, attr1_norm, attr1_stride, attr1_buffer;
    void* attr1_pointer = nullptr;
    GLint active_texture, texture, unpack_alignment;

    explicit SavedState(const Gl& g) : gl(g) {
        gl.GetIntegerv(GL_CURRENT_PROGRAM, &program);
        gl.GetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buffer);
        gl.GetIntegerv(GL_VIEWPORT, viewport);
        gl.GetIntegerv(GL_BLEND_SRC_RGB, &blend_src_rgb);
        gl.GetIntegerv(GL_BLEND_DST_RGB, &blend_dst_rgb);
        gl.GetIntegerv(GL_BLEND_SRC_ALPHA, &blend_src_a);
        gl.GetIntegerv(GL_BLEND_DST_ALPHA, &blend_dst_a);
        gl.GetIntegerv(GL_BLEND_EQUATION_RGB, &blend_eq_rgb);
        gl.GetIntegerv(GL_BLEND_EQUATION_ALPHA, &blend_eq_a);
        gl.GetBooleanv(GL_COLOR_WRITEMASK, color_mask);
        blend = gl.IsEnabled(GL_BLEND);
        depth = gl.IsEnabled(GL_DEPTH_TEST);
        stencil = gl.IsEnabled(GL_STENCIL_TEST);
        scissor = gl.IsEnabled(GL_SCISSOR_TEST);
        cull = gl.IsEnabled(GL_CULL_FACE);
        gl.GetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &attr_enabled);
        gl.GetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_SIZE, &attr_size);
        gl.GetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_TYPE, &attr_type);
        gl.GetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &attr_norm);
        gl.GetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &attr_stride);
        gl.GetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &attr_buffer);
        gl.GetVertexAttribPointerv(0, GL_VERTEX_ATTRIB_ARRAY_POINTER, &attr_pointer);
        gl.GetVertexAttribiv(1, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &attr1_enabled);
        gl.GetVertexAttribiv(1, GL_VERTEX_ATTRIB_ARRAY_SIZE, &attr1_size);
        gl.GetVertexAttribiv(1, GL_VERTEX_ATTRIB_ARRAY_TYPE, &attr1_type);
        gl.GetVertexAttribiv(1, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &attr1_norm);
        gl.GetVertexAttribiv(1, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &attr1_stride);
        gl.GetVertexAttribiv(1, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &attr1_buffer);
        gl.GetVertexAttribPointerv(1, GL_VERTEX_ATTRIB_ARRAY_POINTER, &attr1_pointer);
        gl.GetIntegerv(GL_ACTIVE_TEXTURE, &active_texture);
        gl.ActiveTexture(GL_TEXTURE0);
        gl.GetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
        gl.GetIntegerv(GL_UNPACK_ALIGNMENT, &unpack_alignment);
    }
    ~SavedState() {
        auto set = [&](GLenum cap, GLboolean on) { on ? gl.Enable(cap) : gl.Disable(cap); };
        gl.BindTexture(GL_TEXTURE_2D, GLuint(texture));
        gl.ActiveTexture(GLenum(active_texture));
        gl.PixelStorei(GL_UNPACK_ALIGNMENT, unpack_alignment);
        gl.BindBuffer(GL_ARRAY_BUFFER, GLuint(attr1_buffer));
        gl.VertexAttribPointer(1, attr1_size, GLenum(attr1_type), GLboolean(attr1_norm), attr1_stride, attr1_pointer);
        attr1_enabled ? gl.EnableVertexAttribArray(1) : gl.DisableVertexAttribArray(1);
        gl.BindBuffer(GL_ARRAY_BUFFER, GLuint(attr_buffer));
        gl.VertexAttribPointer(0, attr_size, GLenum(attr_type), GLboolean(attr_norm), attr_stride, attr_pointer);
        attr_enabled ? gl.EnableVertexAttribArray(0) : gl.DisableVertexAttribArray(0);
        gl.BindBuffer(GL_ARRAY_BUFFER, GLuint(array_buffer));
        gl.UseProgram(GLuint(program));
        gl.Viewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        gl.BlendFuncSeparate(GLenum(blend_src_rgb), GLenum(blend_dst_rgb), GLenum(blend_src_a), GLenum(blend_dst_a));
        gl.BlendEquationSeparate(GLenum(blend_eq_rgb), GLenum(blend_eq_a));
        gl.ColorMask(color_mask[0], color_mask[1], color_mask[2], color_mask[3]);
        set(GL_BLEND, blend);
        set(GL_DEPTH_TEST, depth);
        set(GL_STENCIL_TEST, stencil);
        set(GL_SCISSOR_TEST, scissor);
        set(GL_CULL_FACE, cull);
    }
};

}  // namespace

void Overlay::ring(float x, float y, float outer, float inner, uint32_t rgba) {
    shapes_.push_back({x, y, outer, inner, rgba});
}

void Overlay::image(const uint8_t* rgba, int w, int h, uint64_t generation, float x, float y, float width,
                    float height, int turns) {
    image_.rgba = rgba;
    image_.w = w;
    image_.h = h;
    image_.generation = generation;
    image_.x = x;
    image_.y = y;
    image_.width = width;
    image_.height = height;
    image_.turns = turns;
}

void Overlay::render_image(int width, int height) {
    static const Gl gl;
    static GLuint program = 0, texture = 0;
    static GLint u_size = -1, u_texture = -1;
    static uint64_t uploaded = 0;
    static int tw = 0, th = 0;
    if (!program) {
        program = link(gl, kImageVertex, kImageFragment, "a_uv");
        u_size = gl.GetUniformLocation(program, "u_size");
        u_texture = gl.GetUniformLocation(program, "u_texture");
        gl.GenTextures(1, &texture);
    }
    const Image& im = image_;
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, texture);
    if (uploaded != im.generation || tw != im.w || th != im.h) {
        gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, im.w, im.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, im.rgba);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        uploaded = im.generation;
        tw = im.w;
        th = im.h;
    }
    gl.UseProgram(program);
    gl.Uniform2f(u_size, float(width), float(height));
    gl.Uniform1i(u_texture, 0);
    // corners top left, top right, bottom left, bottom right; their picture coordinates
    // turned with the picture
    const float quad[] = {im.x, im.y, im.x + im.width, im.y, im.x, im.y + im.height, im.x + im.width, im.y + im.height};
    static const float kCorners[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};  // clockwise from top left
    const int order[4] = {0, 1, 3, 2};                                   // quad's corners in that ring
    float uv[8];
    for (int i = 0; i < 4; ++i) {
        const float* c = kCorners[(order[i] - im.turns + 4) % 4];
        uv[i * 2] = c[0];
        uv[i * 2 + 1] = c[1];
    }
    gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, uv);
    gl.EnableVertexAttribArray(1);
    gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl.DisableVertexAttribArray(1);
}

void Overlay::render(int width, int height) {
    if (shapes_.empty() && !image_.rgba) return;
    static const Gl gl;
    static GLuint program = 0;
    static GLint u_size = -1, u_color = -1, u_ring = -1;
    if (!program) {
        program = link(gl, kVertex, kFragment, nullptr);
        u_size = gl.GetUniformLocation(program, "u_size");
        u_color = gl.GetUniformLocation(program, "u_color");
        u_ring = gl.GetUniformLocation(program, "u_ring");
    }

    const SavedState saved(gl);
    gl.UseProgram(program);
    gl.BindBuffer(GL_ARRAY_BUFFER, 0);
    gl.Viewport(0, 0, width, height);
    gl.Disable(GL_DEPTH_TEST);
    gl.Disable(GL_STENCIL_TEST);
    gl.Disable(GL_SCISSOR_TEST);
    gl.Disable(GL_CULL_FACE);
    gl.Enable(GL_BLEND);
    gl.BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    gl.BlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl.EnableVertexAttribArray(0);
    if (image_.rgba && image_.w > 0 && image_.h > 0) render_image(width, height);
    image_ = {};
    gl.UseProgram(program);
    gl.Uniform2f(u_size, float(width), float(height));
    for (const Shape& s : shapes_) {
        const float r = s.outer + 1;
        const float quad[] = {s.x - r, s.y - r, s.x + r, s.y - r, s.x - r, s.y + r, s.x + r, s.y + r};
        gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
        gl.Uniform4f(u_color, (s.rgba >> 24) / 255.0f, (s.rgba >> 16 & 0xff) / 255.0f, (s.rgba >> 8 & 0xff) / 255.0f,
                     (s.rgba & 0xff) / 255.0f);
        gl.Uniform4f(u_ring, s.x, s.y, s.outer, s.inner);
        gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    shapes_.clear();
}

}  // namespace slingshot
