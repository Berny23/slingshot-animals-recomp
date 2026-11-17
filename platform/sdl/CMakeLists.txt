# SPDX-License-Identifier: GPL-3.0-or-later
# Desktop host (SDL3). main.cpp is built into each game's executable (games/).

add_library(slingshot_host OBJECT
    android_input.cpp
    sdl_input.cpp
    sdl_audio.cpp
    mod.cpp
    overlay.cpp
    test_script.cpp
    watchdog.cpp)
target_include_directories(slingshot_host PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
if(NINTENDO_SWITCH)
    target_sources(slingshot_host PRIVATE "${CMAKE_SOURCE_DIR}/platform/switch/switch_host.cpp")
    target_include_directories(slingshot_host PUBLIC "${CMAKE_SOURCE_DIR}/platform/switch")
endif()
target_link_libraries(slingshot_host PUBLIC recomp_runtime slingshot::SDL3)
# System fonts: fontconfig on Linux, the font folders elsewhere.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(slingshot_host PRIVATE font_provider.cpp)
    target_link_libraries(slingshot_host PUBLIC ${CMAKE_DL_LIBS})
else()
    target_sources(slingshot_host PRIVATE font_scan.cpp)
    target_link_libraries(slingshot_host PRIVATE slingshot::freetype)
endif()

# The mods that start enabled, as "name,name" ("all" for every one).
# A mod's name is its directory's with "-" for "_".
string(REPLACE "," ";" default_mods "${SLINGSHOT_DEFAULT_MODS}")
if(default_mods STREQUAL "none" OR NOT SLINGSHOT_MODS)
    set(default_mods "")
endif()
foreach(mod IN LISTS default_mods)
    string(REPLACE "-" "_" dir "${mod}")
    if(NOT mod STREQUAL "all" AND NOT IS_DIRECTORY "${CMAKE_SOURCE_DIR}/mods/${dir}")
        message(FATAL_ERROR "SLINGSHOT_DEFAULT_MODS: no mod '${mod}' (mods/${dir})")
    endif()
endforeach()
string(REPLACE ";" "," default_mods "${default_mods}")
set(SLINGSHOT_DEFAULT_MODS_LIST "${default_mods}" CACHE INTERNAL "")
