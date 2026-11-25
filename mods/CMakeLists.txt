# SPDX-License-Identifier: GPL-3.0-or-later
# Mods: every directory here is one mod; its .cpp files are built into the
# executables and register themselves with SLINGSHOT_MOD (see platform/sdl/mod.hpp).
# A file named <game id>.cpp is built for that game only; a mod with such
# files supports just those games, otherwise every game. common/ holds helpers
# shared by mods. A mod's libraries are listed in its libraries.txt, one
# target per line; lines for the Android manifest (permissions, features) in its
# android_manifest.xml.
# Which mods start enabled is SLINGSHOT_DEFAULT_MODS (by default all but those
# registered with SLINGSHOT_MOD_OFF); --mod, --enable-mod, --disable-mod and
# SLINGSHOT_MODS change that at run time.
file(GLOB game_files CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/games/*/game.json")
set(game_ids "")
foreach(f IN LISTS game_files)
    get_filename_component(d "${f}" DIRECTORY)
    get_filename_component(id "${d}" NAME)
    list(APPEND game_ids "${id}")
endforeach()
set(SLINGSHOT_GAME_IDS "${game_ids}" CACHE INTERNAL "")

# Adds the mods supporting game `id` to `target`.
function(slingshot_add_mods target id)
    file(GLOB mod_dirs LIST_DIRECTORIES true CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/mods/*")
    set(sources "")
    foreach(dir IN LISTS mod_dirs)
        if(NOT IS_DIRECTORY "${dir}")
            continue()
        endif()
        file(GLOB files CONFIGURE_DEPENDS "${dir}/*.cpp")
        set(shared "")
        set(own "")
        set(for_games FALSE)
        foreach(f IN LISTS files)
            get_filename_component(stem "${f}" NAME_WE)
            if(stem IN_LIST SLINGSHOT_GAME_IDS)
                set(for_games TRUE)
                if(stem STREQUAL id)
                    list(APPEND own "${f}")
                endif()
            else()
                list(APPEND shared "${f}")
            endif()
        endforeach()
        get_filename_component(name "${dir}" NAME)
        if(name STREQUAL "common" OR NOT for_games OR own)
            list(APPEND sources ${shared} ${own})
            if(EXISTS "${dir}/libraries.txt")
                file(STRINGS "${dir}/libraries.txt" libraries)
                target_link_libraries(${target} PRIVATE ${libraries})
            endif()
            if(EXISTS "${dir}/android_manifest.xml")
                file(READ "${dir}/android_manifest.xml" lines)
                set_property(GLOBAL APPEND_STRING PROPERTY SLINGSHOT_ANDROID_MANIFEST_${id} "${lines}")
                set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${dir}/android_manifest.xml")
            endif()
        endif()
    endforeach()
    target_sources(${target} PRIVATE ${sources})
    target_include_directories(${target} PRIVATE "${CMAKE_SOURCE_DIR}/mods")
endfunction()
