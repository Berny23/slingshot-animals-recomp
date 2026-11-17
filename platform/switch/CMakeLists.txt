# SPDX-License-Identifier: GPL-3.0-or-later
# Nintendo Switch: SDL3's API on devkitPro's SDL2 (slingshot::SDL3), with
# SDL3's headers from its source release.
set(portlibs "${DEVKITPRO}/portlibs/switch/lib")
add_library(slingshot_sdl3_on_sdl2 STATIC sdl3_on_sdl2.cpp sdl2_bridge.cpp)
target_include_directories(slingshot_sdl3_on_sdl2 PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include" "${sdl3_SOURCE_DIR}/include")
set_source_files_properties(sdl2_bridge.cpp PROPERTIES INCLUDE_DIRECTORIES "${DEVKITPRO}/portlibs/switch/include")
target_link_libraries(slingshot_sdl3_on_sdl2 PUBLIC
    "${portlibs}/libSDL2.a" "${portlibs}/libEGL.a" "${portlibs}/libglapi.a" "${portlibs}/libdrm_nouveau.a" nx)

add_library(slingshot_sdl3_headers INTERFACE)
target_include_directories(slingshot_sdl3_headers INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include" "${sdl3_SOURCE_DIR}/include")
