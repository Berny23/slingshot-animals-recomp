# SPDX-License-Identifier: GPL-3.0-or-later
# Mods: every directory here is one mod; its .cpp files are built into the
# host and register themselves with SLINGSHOT_MOD (see platform/sdl/mod.hpp).
# common/ holds helpers shared by mods.
# Which of them start enabled is SLINGSHOT_DEFAULT_MODS (all by default);
# --mod, --no-mod and SLINGSHOT_MODS change that at run time.
file(GLOB mod_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*/*.cpp")

find_package(SDL3 REQUIRED CONFIG)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBLZMA REQUIRED IMPORTED_TARGET liblzma)  # common/game_scripts
add_library(slingshot_mods OBJECT ${mod_sources})
target_include_directories(slingshot_mods PRIVATE "${CMAKE_SOURCE_DIR}/platform/sdl" "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(slingshot_mods PUBLIC PkgConfig::LIBLZMA PRIVATE SDL3::SDL3 recomp_runtime)
