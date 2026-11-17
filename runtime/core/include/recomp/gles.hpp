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
// A GLSL ES 1.00 shader as the host context compiles it.
std::string host_shader(const std::string& es_source);

}  // namespace recomp::gles
