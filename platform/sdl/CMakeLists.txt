# SPDX-License-Identifier: GPL-3.0-or-later
# Desktop host (SDL3).
find_package(SDL3 REQUIRED CONFIG)
find_package(Fontconfig REQUIRED)

add_executable(slingshot
    main.cpp
    android_input.cpp
    sdl_input.cpp
    sdl_audio.cpp
    font_provider.cpp
    mod.cpp
    overlay.cpp
    test_script.cpp
    watchdog.cpp)
target_include_directories(slingshot PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(slingshot PRIVATE recomp_runtime recomp_game_code SDL3::SDL3 Fontconfig::Fontconfig)
set_target_properties(slingshot PROPERTIES ENABLE_EXPORTS ON)  # symbol names in crash backtraces
target_compile_definitions(slingshot PRIVATE "GAME_DIR_DEFAULT=\"${GAME_DIR}\"")

if(SLINGSHOT_MODS)
    target_link_libraries(slingshot PRIVATE slingshot_mods)
    # The mods that start enabled, as "name,name" ("all" for every one).
    # A mod's name is its directory's with "-" for "_".
    string(REPLACE "," ";" default_mods "${SLINGSHOT_DEFAULT_MODS}")
    if(default_mods STREQUAL "none")
        set(default_mods "")
    endif()
    foreach(mod IN LISTS default_mods)
        string(REPLACE "-" "_" dir "${mod}")
        if(NOT mod STREQUAL "all" AND NOT IS_DIRECTORY "${CMAKE_SOURCE_DIR}/mods/${dir}")
            message(FATAL_ERROR "SLINGSHOT_DEFAULT_MODS: no mod '${mod}' (mods/${dir})")
        endif()
    endforeach()
    string(REPLACE ";" "," default_mods "${default_mods}")
    target_compile_definitions(slingshot PRIVATE "SLINGSHOT_DEFAULT_MODS=\"${default_mods}\"")
endif()
