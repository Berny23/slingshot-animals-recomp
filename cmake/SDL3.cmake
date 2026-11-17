# SPDX-License-Identifier: GPL-3.0-or-later
# The pinned SDL3 release and its options, for Dependencies.cmake and
# platform/linux/sdl3 (the prebuilt SDL3 the Linux builder app carries).
include(FetchContent)
if(SLINGSHOT_SDL3_INSTALL)
    set(exclude)
else()
    set(exclude EXCLUDE_FROM_ALL)
endif()
FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.20/SDL3-3.4.20.tar.gz
    URL_HASH SHA256=f3bb06f55bed9557227a700af2ea62e49497ebdf58037732a731a5fb3ef8581f
    ${exclude} SYSTEM)
# Android's SDLActivity loads libSDL3.so; elsewhere SDL is linked in.
if(ANDROID)
    set(SDL_SHARED ON)
    set(SDL_STATIC OFF)
else()
    set(SDL_SHARED OFF)
    set(SDL_STATIC ON)
endif()
set(SDL_TEST_LIBRARY OFF)
set(SDL_TESTS OFF)
set(SDL_EXAMPLES OFF)
