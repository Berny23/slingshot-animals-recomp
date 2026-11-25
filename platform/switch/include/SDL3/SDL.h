// SPDX-License-Identifier: GPL-3.0-or-later
// SDL3 on SDL2 (Nintendo Switch homebrew has only devkitPro's SDL2): SDL3's own
// headers give the types and constants; the functions the host and mods call
// are renamed to sdl3c_* and implemented in platform/switch/sdl3_on_sdl2.cpp.
#pragma once

#define SDL_Init sdl3c_Init
#define SDL_InitSubSystem sdl3c_InitSubSystem
#define SDL_WasInit sdl3c_WasInit
#define SDL_Quit sdl3c_Quit
#define SDL_GetError sdl3c_GetError
#define SDL_SetHint sdl3c_SetHint
#define SDL_SetAppMetadata sdl3c_SetAppMetadata
#define SDL_PollEvent sdl3c_PollEvent
#define SDL_WaitEvent sdl3c_WaitEvent
#define SDL_Delay sdl3c_Delay
#define SDL_GetTicksNS sdl3c_GetTicksNS
#define SDL_free sdl3c_free
#define SDL_GetBasePath sdl3c_GetBasePath
#define SDL_GetPrefPath sdl3c_GetPrefPath
#define SDL_GetPreferredLocales sdl3c_GetPreferredLocales
#define SDL_CreateWindow sdl3c_CreateWindow
#define SDL_DestroyWindow sdl3c_DestroyWindow
#define SDL_GetWindowFlags sdl3c_GetWindowFlags
#define SDL_GetWindowID sdl3c_GetWindowID
#define SDL_SetWindowFullscreen sdl3c_SetWindowFullscreen
#define SDL_SetWindowIcon sdl3c_SetWindowIcon
#define SDL_GetWindowSize sdl3c_GetWindowSize
#define SDL_GetWindowSizeInPixels sdl3c_GetWindowSizeInPixels
#define SDL_GetWindowPixelDensity sdl3c_GetWindowPixelDensity
#define SDL_GetWindowDisplayScale sdl3c_GetWindowDisplayScale
#define SDL_LoadPNG sdl3c_LoadPNG
#define SDL_DestroySurface sdl3c_DestroySurface
#define SDL_IOFromFile sdl3c_IOFromFile
#define SDL_WriteIO sdl3c_WriteIO
#define SDL_CloseIO sdl3c_CloseIO
#define SDL_GetMouseState sdl3c_GetMouseState
#define SDL_GetKeyFromScancode sdl3c_GetKeyFromScancode
#define SDL_GL_SetAttribute sdl3c_GL_SetAttribute
#define SDL_GL_CreateContext sdl3c_GL_CreateContext
#define SDL_GL_DestroyContext sdl3c_GL_DestroyContext
#define SDL_GL_GetProcAddress sdl3c_GL_GetProcAddress
#define SDL_GL_ExtensionSupported sdl3c_GL_ExtensionSupported
#define SDL_GL_SetSwapInterval sdl3c_GL_SetSwapInterval
#define SDL_GL_SwapWindow sdl3c_GL_SwapWindow
#define SDL_GL_UnloadLibrary sdl3c_GL_UnloadLibrary
#define SDL_OpenGamepad sdl3c_OpenGamepad
#define SDL_CloseGamepad sdl3c_CloseGamepad
#define SDL_GetGamepadButton sdl3c_GetGamepadButton
#define SDL_OpenAudioDeviceStream sdl3c_OpenAudioDeviceStream
#define SDL_DestroyAudioStream sdl3c_DestroyAudioStream
#define SDL_PutAudioStreamData sdl3c_PutAudioStreamData
#define SDL_ResumeAudioStreamDevice sdl3c_ResumeAudioStreamDevice
#define SDL_GetAudioStreamDevice sdl3c_GetAudioStreamDevice
#define SDL_GetAudioDeviceFormat sdl3c_GetAudioDeviceFormat
#define SDL_GetCurrentAudioDriver sdl3c_GetCurrentAudioDriver

#include_next <SDL3/SDL.h>
