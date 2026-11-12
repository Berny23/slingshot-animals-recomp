# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.24)
project(slingshot_animals_recomp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# The user supplies the original APK; nothing from it is part of this repository.
set(GAME_APK "" CACHE FILEPATH "Path to the original APK (armeabi-v7a build 8.0.3)")
set(GAME_DIR "${CMAKE_BINARY_DIR}/game" CACHE PATH "Where the APK's library and assets are extracted")
option(SLINGSHOT_MODS "Build the mods in mods/ into the host" ON)
set(SLINGSHOT_DEFAULT_MODS "all" CACHE STRING
    "Mods enabled when the game is started without --mod or SLINGSHOT_MODS: all, none, or a list such as hide-dead-services;offline-shop")

add_subdirectory(tools/recompiler)

set(GAME_LIB "${GAME_DIR}/lib/libAngryBirdsClassic.so")
if(GAME_APK AND NOT EXISTS "${GAME_LIB}")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    message(STATUS "Extracting ${GAME_APK}")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/scripts/extract_apk.py" "${GAME_APK}" "${GAME_DIR}"
        RESULT_VARIABLE extract_result)
    if(NOT extract_result EQUAL 0)
        message(FATAL_ERROR "APK extraction failed")
    endif()
endif()

add_subdirectory(runtime)

if(EXISTS "${GAME_LIB}")
    add_subdirectory(game)
    if(SLINGSHOT_MODS)
        add_subdirectory(mods)
    endif()
    add_subdirectory(platform/sdl)
else()
    message(WARNING "No game library found: configure with -DGAME_APK=/path/to/game.apk to build the game.")
endif()
