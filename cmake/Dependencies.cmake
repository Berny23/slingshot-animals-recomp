# SPDX-License-Identifier: GPL-3.0-or-later
# Libraries, as the targets slingshot::<name>: the system's (Linux), or built
# from pinned source releases (SLINGSHOT_SYSTEM_DEPS=OFF, the default for
# other systems). Each download is checked against its SHA-256.
include(FetchContent)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_CROSSCOMPILING)
    set(system_default ON)
else()
    set(system_default OFF)
endif()
option(SLINGSHOT_SYSTEM_DEPS "Use the system's SDL3, FreeType, HarfBuzz, liblzma, zlib and capstone" ${system_default})

# Only the tools (abrecomp, slingshot-apk): zlib and capstone, no game libraries.
option(SLINGSHOT_TOOLS_ONLY "Build only abrecomp and slingshot-apk" OFF)
# Host tools (abrecomp, slingshot-apk) from another build, for cross builds.
set(SLINGSHOT_HOST_TOOLS "" CACHE PATH "Directory with abrecomp and slingshot-apk built for this machine")
# A prebuilt static SDL3 (an install prefix, see platform/linux/sdl3) instead of
# building it, when the other libraries are downloaded.
set(SLINGSHOT_SDL3_PREFIX "" CACHE PATH "Install prefix of a prebuilt SDL3")

function(slingshot_alias name target)
    add_library(slingshot::${name} INTERFACE IMPORTED GLOBAL)
    target_link_libraries(slingshot::${name} INTERFACE ${target})
endfunction()

# QR codes and pictures for mods (slingshot::qr): always the pinned sources.
if(NOT SLINGSHOT_TOOLS_ONLY)
    FetchContent_Declare(qrcodegen
        URL https://github.com/nayuki/QR-Code-generator/archive/refs/tags/v1.8.0.tar.gz
        URL_HASH SHA256=2ec0a4d33d6f521c942eeaf473d42d5fe139abcfa57d2beffe10c5cf7d34ae60
        SOURCE_SUBDIR none EXCLUDE_FROM_ALL SYSTEM)
    FetchContent_Declare(quirc
        URL https://github.com/dlbeer/quirc/archive/refs/tags/v1.2.tar.gz
        URL_HASH SHA256=73c12ea33d337ec38fb81218c7674f57dba7ec0570bddd5c7f7a977c0deb64c5
        SOURCE_SUBDIR none EXCLUDE_FROM_ALL SYSTEM)
    FetchContent_Declare(stb
        URL https://github.com/nothings/stb/archive/6b5799bae8a1fe3792691ef4b72ae26434c3e799.tar.gz
        URL_HASH SHA256=87e9ecd2d2e0cb8fa35bf5a93ea6ca3999595ea4f0a4738ed1aac0a34cbd4da9
        SOURCE_SUBDIR none EXCLUDE_FROM_ALL SYSTEM)
    FetchContent_MakeAvailable(qrcodegen quirc stb)
    add_library(slingshot_qr STATIC
        "${qrcodegen_SOURCE_DIR}/c/qrcodegen.c"
        "${quirc_SOURCE_DIR}/lib/decode.c"
        "${quirc_SOURCE_DIR}/lib/identify.c"
        "${quirc_SOURCE_DIR}/lib/quirc.c"
        "${quirc_SOURCE_DIR}/lib/version_db.c")
    target_include_directories(slingshot_qr SYSTEM PUBLIC
        "${qrcodegen_SOURCE_DIR}/c" "${quirc_SOURCE_DIR}/lib" "${stb_SOURCE_DIR}")
    set_target_properties(slingshot_qr PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
    slingshot_alias(qr slingshot_qr)
endif()

if(SLINGSHOT_SYSTEM_DEPS AND SLINGSHOT_TOOLS_ONLY)
    find_package(PkgConfig REQUIRED)
    find_package(ZLIB REQUIRED)
    pkg_check_modules(CAPSTONE REQUIRED IMPORTED_TARGET capstone>=5)
    slingshot_alias(zlib ZLIB::ZLIB)
    slingshot_alias(capstone PkgConfig::CAPSTONE)
    return()
elseif(SLINGSHOT_SYSTEM_DEPS)
    find_package(PkgConfig REQUIRED)
    find_package(SDL3 REQUIRED CONFIG)
    find_package(Freetype REQUIRED)
    find_package(ZLIB REQUIRED)
    pkg_check_modules(HARFBUZZ REQUIRED IMPORTED_TARGET harfbuzz)
    pkg_check_modules(LIBLZMA REQUIRED IMPORTED_TARGET liblzma)
    slingshot_alias(SDL3 SDL3::SDL3)
    slingshot_alias(SDL3_headers SDL3::Headers)
    slingshot_alias(freetype Freetype::Freetype)
    slingshot_alias(harfbuzz PkgConfig::HARFBUZZ)
    slingshot_alias(lzma PkgConfig::LIBLZMA)
    slingshot_alias(zlib ZLIB::ZLIB)
    if(NOT SLINGSHOT_HOST_TOOLS)
        pkg_check_modules(CAPSTONE REQUIRED IMPORTED_TARGET capstone>=5)
        slingshot_alias(capstone PkgConfig::CAPSTONE)
    endif()
    return()
endif()

set(BUILD_SHARED_LIBS OFF)
set(BUILD_TESTING OFF)

FetchContent_Declare(zlib
    URL https://github.com/madler/zlib/releases/download/v1.4/zlib-1.4.tar.xz
    URL_HASH SHA256=4f11c512053d7bab8d9e9b89bde3575add3e4c50a383b42ef6cb837035fc5a7c
    EXCLUDE_FROM_ALL SYSTEM)
set(ZLIB_BUILD_TESTING OFF)
set(ZLIB_BUILD_SHARED OFF)
set(ZLIB_BUILD_STATIC ON)
set(ZLIB_INSTALL OFF)

FetchContent_Declare(freetype
    URL https://download.savannah.gnu.org/releases/freetype/freetype-2.15.0.tar.xz
    URL_HASH SHA256=93cd47eb719c325107f2d3cf087d06299e944c3b48a40b55537302304dea1dd5
    EXCLUDE_FROM_ALL SYSTEM)
foreach(dep ZLIB BZIP2 PNG HARFBUZZ BROTLI)
    set(FT_DISABLE_${dep} ON)
endforeach()

FetchContent_Declare(harfbuzz
    URL https://github.com/harfbuzz/harfbuzz/releases/download/15.1.0/harfbuzz-15.1.0.tar.xz
    URL_HASH SHA256=0fc7c565443a5f4be7da65ac5ee5bd737d9b158dc908708b7689a0a5796b0c8d
    EXCLUDE_FROM_ALL SYSTEM)
set(HB_BUILD_SUBSET OFF)
set(HB_HAVE_CORETEXT OFF)

FetchContent_Declare(xz
    URL https://github.com/tukaani-project/xz/releases/download/v5.8.4/xz-5.8.4.tar.xz
    URL_HASH SHA256=4ce24038fd4221e0d13bc1a2de7a4db56e90b92b3bf75321f6c14be73f65de4b
    EXCLUDE_FROM_ALL SYSTEM)
set(XZ_NLS OFF)
set(XZ_TOOL_XZ OFF)
set(XZ_TOOL_XZDEC OFF)
set(XZ_TOOL_LZMADEC OFF)
set(XZ_TOOL_LZMAINFO OFF)
set(XZ_TOOL_SCRIPTS OFF)
set(XZ_DOC OFF)

FetchContent_Declare(capstone
    URL https://github.com/capstone-engine/capstone/archive/refs/tags/5.0.6.tar.gz
    URL_HASH SHA256=240ebc834c51aae41ca9215d3190cc372fd132b9c5c8aa2d5f19ca0c325e28f9
    EXCLUDE_FROM_ALL SYSTEM)
set(CAPSTONE_ARCHITECTURE_DEFAULT OFF)
set(CAPSTONE_ARM_SUPPORT ON)
set(CAPSTONE_BUILD_TESTS OFF)
set(CAPSTONE_BUILD_CSTOOL OFF)
set(CAPSTONE_INSTALL OFF)

include(${CMAKE_CURRENT_LIST_DIR}/SDL3.cmake)
set(SDL_INSTALL OFF)

if(SLINGSHOT_TOOLS_ONLY)
    set(deps zlib)
else()
    set(deps zlib freetype harfbuzz xz)
    if(SLINGSHOT_SDL3_PREFIX)
        find_package(SDL3 REQUIRED CONFIG PATHS "${SLINGSHOT_SDL3_PREFIX}" NO_DEFAULT_PATH)
    else()
        list(APPEND deps SDL3)
    endif()
endif()
if(NOT SLINGSHOT_HOST_TOOLS)
    list(APPEND deps capstone)
endif()
FetchContent_MakeAvailable(${deps})
if(ANDROID AND NOT SLINGSHOT_TOOLS_ONLY)
    target_link_options(SDL3-shared PRIVATE $<$<NOT:$<CONFIG:Debug>>:-s>)
endif()

slingshot_alias(zlib zlibstatic)
if(NOT SLINGSHOT_HOST_TOOLS)
    slingshot_alias(capstone capstone_static)
endif()
if(SLINGSHOT_TOOLS_ONLY)
    return()
endif()
slingshot_alias(freetype freetype)
slingshot_alias(harfbuzz harfbuzz)
slingshot_alias(lzma liblzma)
target_include_directories(slingshot::lzma INTERFACE "${xz_SOURCE_DIR}/src/liblzma/api")
if(NINTENDO_SWITCH)
    add_subdirectory("${CMAKE_SOURCE_DIR}/platform/switch" "${CMAKE_BINARY_DIR}/platform-switch")
    slingshot_alias(SDL3 slingshot_sdl3_on_sdl2)
    slingshot_alias(SDL3_headers slingshot_sdl3_headers)
else()
    slingshot_alias(SDL3 SDL3::SDL3)
    slingshot_alias(SDL3_headers SDL3::Headers)
endif()
