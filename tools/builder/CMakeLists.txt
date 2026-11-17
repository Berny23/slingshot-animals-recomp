# SPDX-License-Identifier: GPL-3.0-or-later
# slingshot-builder: the app that builds and installs a game from its APK
# (Qt 6 Widgets). A project of its own: cmake -S tools/builder -B <dir>.
cmake_minimum_required(VERSION 3.28)
project(slingshot_builder VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

get_filename_component(repo "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(SLINGSHOT_VERSION "dev" CACHE STRING "Version shown in the app (the release tag)")

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Network Svg)

# zlib for the APK reader, built in (no system library needed).
include(FetchContent)
FetchContent_Declare(zlib
    URL https://github.com/madler/zlib/releases/download/v1.4/zlib-1.4.tar.xz
    URL_HASH SHA256=4f11c512053d7bab8d9e9b89bde3575add3e4c50a383b42ef6cb837035fc5a7c
    EXCLUDE_FROM_ALL SYSTEM)
set(ZLIB_BUILD_TESTING OFF)
set(ZLIB_BUILD_SHARED OFF)
set(ZLIB_BUILD_STATIC ON)
set(ZLIB_INSTALL OFF)
FetchContent_MakeAvailable(zlib)

add_library(builder_apk STATIC
    ${repo}/tools/apk/src/android_res.cpp
    ${repo}/tools/apk/src/extract.cpp
    ${repo}/tools/apk/src/sha256.cpp
    ${repo}/tools/apk/src/zip.cpp)
target_include_directories(builder_apk PUBLIC ${repo}/tools/apk/src)
target_link_libraries(builder_apk PUBLIC zlibstatic)

add_executable(slingshot-builder
    src/main.cpp
    src/apk_match.cpp
    src/catalog.cpp
    src/devkitpro.cpp
    src/downloads.cpp
    src/install.cpp
    src/job.cpp
    src/paths.cpp
    src/window.cpp
    resources/builder.qrc)
target_link_libraries(slingshot-builder PRIVATE builder_apk Qt6::Widgets Qt6::Network Qt6::Svg)

option(SLINGSHOT_BUILDER_BREEZE "Build KDE's Breeze style in and use it on every platform" ON)
if(SLINGSHOT_BUILDER_BREEZE)
    add_subdirectory(breeze)
    target_link_libraries(slingshot-builder PRIVATE builder_breeze)
    target_compile_definitions(slingshot-builder PRIVATE SLINGSHOT_BREEZE=1)
endif()
target_compile_definitions(slingshot-builder PRIVATE
    SLINGSHOT_SOURCE_DIR="${repo}"
    SLINGSHOT_VERSION="${SLINGSHOT_VERSION}")
if(WIN32)
    # The single-file release: launcher/launcher.cpp with the deployed folder appended.
    add_executable(slingshot-builder-launcher WIN32 launcher/launcher.cpp launcher/launcher.rc)
    target_link_libraries(slingshot-builder-launcher PRIVATE builder_apk comctl32)
    target_compile_definitions(slingshot-builder-launcher PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    target_link_options(slingshot-builder-launcher PRIVATE -static -municode $<$<NOT:$<CONFIG:Debug>>:-s>)
    target_compile_definitions(slingshot-builder PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_sources(slingshot-builder PRIVATE resources/builder.rc)
    set_target_properties(slingshot-builder PROPERTIES WIN32_EXECUTABLE ON)
elseif(APPLE)
    set_target_properties(slingshot-builder PROPERTIES
        MACOSX_BUNDLE ON
        OUTPUT_NAME "Slingshot Builder"
        MACOSX_BUNDLE_BUNDLE_NAME "Slingshot Builder"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.berny23.slingshot-builder"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE builder.icns)
    target_sources(slingshot-builder PRIVATE resources/builder.icns)
    set_source_files_properties(resources/builder.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()
