// SPDX-License-Identifier: GPL-3.0-or-later
// OpenGL ES 2.0 forwarding (runtime/hle/gles).
#pragma once

namespace recomp::gles {

// Resolves a GLES2 entry point of the current host context (for example
// SDL_GL_GetProcAddress). Must be set before the game issues GL calls.
using Loader = void* (*)(const char* name);
void set_loader(Loader loader);

}  // namespace recomp::gles
