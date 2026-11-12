# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.24)
project(slingshot_animals_recomp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# The user supplies the original APKs; nothing from them is part of this repository.
set(GAME_APKS "" CACHE STRING "APKs of the supported games (games/*/game.json), separated by ;")
set(GAME_ROOT "${CMAKE_BINARY_DIR}/games" CACHE PATH "Where the APKs' libraries and assets are extracted (<root>/<game id>)")
option(SLINGSHOT_MODS "Build the mods in mods/ into the executables" ON)
set(SLINGSHOT_DEFAULT_MODS "all" CACHE STRING
    "Mods enabled when a game is started without --mod or SLINGSHOT_MODS: all, none, or a list such as hide-dead-services;offline-shop")

add_subdirectory(tools/recompiler)

# Extract each APK once (a stamp per APK path).
foreach(apk IN LISTS GAME_APKS)
    string(MD5 key "${apk}")
    set(stamp "${GAME_ROOT}/.extracted-${key}")
    if(NOT EXISTS "${stamp}")
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        message(STATUS "Extracting ${apk}")
        execute_process(
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/scripts/extract_apk.py"
                    "${CMAKE_SOURCE_DIR}/games" "${GAME_ROOT}" "${apk}"
            RESULT_VARIABLE extract_result)
        if(NOT extract_result EQUAL 0)
            message(FATAL_ERROR "APK extraction failed: ${apk}")
        endif()
        file(WRITE "${stamp}" "${apk}\n")
    endif()
endforeach()

add_subdirectory(runtime)
add_subdirectory(platform/sdl)
if(SLINGSHOT_MODS)
    add_subdirectory(mods)
endif()
add_subdirectory(games)
