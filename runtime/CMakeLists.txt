# SPDX-License-Identifier: GPL-3.0-or-later
# Runtime: CPU support, loader/dispatch, and the HLE modules that replace the
# game's imports. Built as an OBJECT library so that the static registrations
# in HLE modules are always linked in.
#
# HLE modules are picked up per directory; dropping a new .cpp file into one of
# them is enough to add imports.
file(GLOB RECOMP_CORE_SOURCES CONFIGURE_DEPENDS core/src/*.cpp)
file(GLOB RECOMP_HLE_SOURCES CONFIGURE_DEPENDS hle/libc/*.cpp hle/android/*.cpp hle/gles/*.cpp hle/fusion/*.cpp jni/*.cpp jni/classes/*.cpp)

add_library(recomp_runtime OBJECT ${RECOMP_CORE_SOURCES} ${RECOMP_HLE_SOURCES})
target_include_directories(recomp_runtime PUBLIC core/include jni/include PRIVATE core/src)
target_compile_options(recomp_runtime PRIVATE -Wall -Wextra -fno-strict-aliasing)
target_compile_features(recomp_runtime PUBLIC cxx_std_20)
find_package(Threads REQUIRED)
find_package(Freetype REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(HARFBUZZ REQUIRED IMPORTED_TARGET harfbuzz)
target_link_libraries(recomp_runtime PUBLIC Threads::Threads PRIVATE Freetype::Freetype PkgConfig::HARFBUZZ)
