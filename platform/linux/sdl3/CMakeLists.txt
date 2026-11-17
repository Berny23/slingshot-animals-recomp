# SPDX-License-Identifier: GPL-3.0-or-later
# The prebuilt SDL3 the Linux builder app carries (SLINGSHOT_SDL3_PREFIX): the
# pinned release as a static library with every video and audio backend, each
# loaded at run time, so a game build needs no X11, Wayland or audio headers.
#   cmake -S platform/linux/sdl3 -B <dir> && cmake --build <dir> && cmake --install <dir> --prefix <prefix>
cmake_minimum_required(VERSION 3.28)
project(slingshot_sdl3 LANGUAGES C CXX)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(SLINGSHOT_SDL3_INSTALL ON)
include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SDL3.cmake)
set(SDL_INSTALL ON)
set(SDL_DEPS_SHARED ON)
foreach(backend X11 WAYLAND WAYLAND_LIBDECOR KMSDRM PIPEWIRE PULSEAUDIO ALSA)
    set(SDL_${backend} ON)
    set(SDL_${backend}_SHARED ON)
endforeach()
set(SDL_X11_XCURSOR ON)
set(SDL_X11_XINPUT ON)
FetchContent_MakeAvailable(SDL3)
# Each backend must be in, loaded at run time (checked when installing, once
# SDL_build_config.h exists).
install(CODE "set(SDL3_BUILD_DIR [[${sdl3_BINARY_DIR}]])")
install(CODE [[
    file(GLOB_RECURSE config "${SDL3_BUILD_DIR}/*/SDL_build_config.h")
    file(READ "${config}" config)
    foreach(define VIDEO_DRIVER_X11_DYNAMIC VIDEO_DRIVER_WAYLAND_DYNAMIC VIDEO_DRIVER_WAYLAND_DYNAMIC_LIBDECOR
                   AUDIO_DRIVER_PIPEWIRE_DYNAMIC AUDIO_DRIVER_PULSEAUDIO_DYNAMIC AUDIO_DRIVER_ALSA_DYNAMIC)
        if(NOT config MATCHES "#define SDL_${define} \"")
            message(FATAL_ERROR "SDL3 was built without SDL_${define}: install the development packages")
        endif()
    endforeach()
]])
