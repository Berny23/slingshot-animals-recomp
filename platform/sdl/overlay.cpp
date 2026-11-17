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

// Saves the state Overlay::render touches and restores it on destruction.
struct SavedState {
    const Gl& gl;
    GLint program, array_buffer, viewport[4];
    GLint blend_src_rgb, blend_dst_rgb, blend_src_a, blend_dst_a, blend_eq_rgb, blend_eq_a;
    GLboolean blend, depth, stencil, scissor, cull, color_mask[4];
    GLint attr_enabled, attr_size, attr_type, attr_norm, attr_stride, attr_buffer;
    void* attr_pointer = nullptr;

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
    }
    ~SavedState() {
        auto set = [&](GLenum cap, GLboolean on) { on ? gl.Enable(cap) : gl.Disable(cap); };
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

void Overlay::render(int width, int height) {
    if (shapes_.empty()) return;
    static const Gl gl;
    static GLuint program = 0;
    static GLint u_size = -1, u_color = -1, u_ring = -1;
    if (!program) {
        auto compile = [&](GLenum type, const char* src) {
            const GLuint s = gl.CreateShader(type);
            const std::string text = recomp::gles::host_shader(src);
            const char* p = text.c_str();
            gl.ShaderSource(s, 1, &p, nullptr);
            gl.CompileShader(s);
            return s;
        };
        program = gl.CreateProgram();
        gl.AttachShader(program, compile(GL_VERTEX_SHADER, kVertex));
        gl.AttachShader(program, compile(GL_FRAGMENT_SHADER, kFragment));
        gl.BindAttribLocation(program, 0, "a_pos");
        gl.LinkProgram(program);
        GLint ok = 0;
        gl.GetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) std::fprintf(stderr, "host: overlay shader failed to link\n");
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
    gl.Uniform2f(u_size, float(width), float(height));
    gl.EnableVertexAttribArray(0);
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
