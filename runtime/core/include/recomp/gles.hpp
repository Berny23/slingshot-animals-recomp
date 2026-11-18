// SPDX-License-Identifier: GPL-3.0-or-later
// OpenGL ES 2.0 forwarding (runtime/hle/gles).
#pragma once

#include <string>

namespace recomp::gles {

// Resolves a GLES2 entry point of the current host context (for example
// SDL_GL_GetProcAddress). Must be set before the game issues GL calls.
using Loader = void* (*)(const char* name);
void set_loader(Loader loader);

// The host context is desktop OpenGL 2.1 instead of OpenGL ES 2.0 (Windows,
// macOS): shaders are translated, ETC1 textures decoded and the version
// strings read as OpenGL ES 2.0. Call after making the context current.
void set_desktop_gl(bool desktop);
bool desktop_gl();
// Where the game's screen (framebuffer 0) is on the surface: x, y, width, height
// in surface pixels (y from the bottom, as in GL) and surface pixels per game
// pixel. The game draws for a screen of width x height divided by `scale`; its
// viewport, scissor, clears and read-backs there are mapped to the area. By
// default the whole surface at scale 1 (width 0: the whole surface).
void set_screen_area(int x, int y, int width, int height, float scale);
// Clears the surface outside the area black; call before the game draws a frame.
void clear_outside_screen(int surface_width, int surface_height);
// A GLSL ES 1.00 shader as the host context compiles it.
std::string host_shader(const std::string& es_source);

}  // namespace recomp::gles
