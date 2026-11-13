# SPDX-License-Identifier: GPL-3.0-or-later
# One executable per extracted game (bin/slingshot-<id>): its recompiled code,
# generated from the user's library at build time, its game_info.cpp from
# games/<id>/game.json, the host and the mods that support the game.
set(GEN_FILE_COUNT 128)  # must match EmitOptions::file_count
# abrecomp emit needs about 750 MB: one at a time (Ninja).
set_property(GLOBAL APPEND PROPERTY JOB_POOLS recompile=1)

function(json_get out json key)
    string(JSON value ERROR_VARIABLE err GET "${json}" "${key}")
    if(err)
        set(value "")
    endif()
    set(${out} "${value}" PARENT_SCOPE)
endfunction()

# Number literal for game_info.cpp ("0x..." or 0).
function(json_addr out json key)
    json_get(value "${json}" "${key}")
    if(value STREQUAL "")
        set(value 0)
    endif()
    set(${out} "${value}" PARENT_SCOPE)
endfunction()

file(GLOB game_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*/game.json")
set(built_games "")
foreach(game_file IN LISTS game_files)
    get_filename_component(game_src "${game_file}" DIRECTORY)
    get_filename_component(id "${game_src}" NAME)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${game_file}")
    file(READ "${game_file}" json)
    json_get(GAME_TITLE "${json}" title)
    json_get(GAME_VERSION "${json}" version)
    string(REGEX REPLACE " .*" "" GAME_VERSION "${GAME_VERSION}")  # "8.0.3 (build 8031)" -> versionName
    json_get(GAME_PACKAGE "${json}" package)
    json_get(GAME_LIBRARY "${json}" library)
    json_get(lib_sha256 "${json}" sha256)
    json_addr(GAME_SCRIPT_KEY "${json}" script_key)
    foreach(part init free encrypt decrypt)
        string(TOUPPER "${part}" upper)
        json_addr(GAME_CIPHER_${upper} "${json}" cipher_${part})
    endforeach()
    json_addr(GAME_LOAD_ASSET "${json}" load_asset)
    json_addr(GAME_LOAD_CONFIG "${json}" load_config)
    json_addr(load_script "${json}" load_script)
    json_get(GAME_ASSET_PREFIX "${json}" asset_prefix)
    json_get(stl "${json}" stl)
    json_get(GAME_MAIN_SCRIPT "${json}" main_script)
    if(GAME_MAIN_SCRIPT STREQUAL "")
        set(GAME_MAIN_SCRIPT "data/scripts/gamelogic.lua")
    endif()
    json_get(GAME_APP_CLASS "${json}" app_class)
    if(GAME_APP_CLASS STREQUAL "")
        set(GAME_APP_CLASS "com/rovio/fusion/NativeApplication")
    endif()
    json_get(script_format "${json}" script_format)
    if(script_format STREQUAL "7z")
        set(GAME_SCRIPTS_7Z true)
    else()
        set(GAME_SCRIPTS_7Z false)
    endif()
    json_get(GAME_INPUT_CLASS "${json}" input_class)
    if(GAME_INPUT_CLASS STREQUAL "")
        set(GAME_INPUT_CLASS "com/rovio/fusion/MyInputHandler")
    endif()
    if(stl STREQUAL "libc++")
        set(GAME_LIBCXX true)
    else()
        set(GAME_LIBCXX false)
    endif()
    set(GAME_ID "${id}")
    set(GAME_DIR "${GAME_ROOT}/${id}")
    set(game_lib "${GAME_DIR}/lib/lib${GAME_LIBRARY}.so")
    if(NOT EXISTS "${game_lib}")
        continue()
    endif()
    list(APPEND built_games "${id}")

    # Recompiled code. The asset loaders accept plain files too
    # (runtime/hle/fusion/script_loader.cpp).
    set(gen_dir "${CMAKE_CURRENT_BINARY_DIR}/${id}/generated")
    set(gen_sources "${gen_dir}/table.cpp")
    math(EXPR last "${GEN_FILE_COUNT} - 1")
    foreach(i RANGE ${last})
        string(LENGTH "${i}" len)
        if(len EQUAL 1)
            set(n "00${i}")
        elseif(len EQUAL 2)
            set(n "0${i}")
        else()
            set(n "${i}")
        endif()
        list(APPEND gen_sources "${gen_dir}/funcs_${n}.cpp")
    endforeach()
    set(wraps "")
    if(NOT GAME_LOAD_ASSET STREQUAL "0")
        list(APPEND wraps --wrap "${GAME_LOAD_ASSET}=fusion_load_asset")
    endif()
    if(NOT GAME_LOAD_CONFIG STREQUAL "0")
        list(APPEND wraps --wrap "${GAME_LOAD_CONFIG}=fusion_load_config")
    endif()
    if(NOT load_script STREQUAL "0")
        list(APPEND wraps --wrap "${load_script}=fusion_load_script")
    endif()
    add_custom_command(
        OUTPUT ${gen_sources} "${gen_dir}/funcs.hpp"
        COMMAND abrecomp emit "${game_lib}" "${gen_dir}" --sha256 "${lib_sha256}" ${wraps}
        DEPENDS abrecomp "${game_lib}" "${game_file}"
        COMMENT "Recompiling lib${GAME_LIBRARY}.so (${GAME_TITLE})"
        JOB_POOL recompile
        VERBATIM)
    add_library(recomp_game_code_${id} OBJECT ${gen_sources})
    target_link_libraries(recomp_game_code_${id} PRIVATE recomp_runtime)
    # Guest floating point must not be contracted into FMAs; guest memory is
    # accessed through byte copies, but keep the optimiser conservative anyway.
    target_compile_options(recomp_game_code_${id} PRIVATE -O2 -ffp-contract=off -fno-strict-aliasing -w)

    configure_file(game_info.cpp.in "${id}/game_info.cpp" @ONLY)

    set(exe slingshot-${id})
    add_executable(${exe} "${CMAKE_CURRENT_BINARY_DIR}/${id}/game_info.cpp" "${CMAKE_SOURCE_DIR}/platform/sdl/main.cpp")
    target_link_libraries(${exe} PRIVATE slingshot_host recomp_runtime recomp_game_code_${id})
    set_target_properties(${exe} PROPERTIES
        ENABLE_EXPORTS ON  # symbol names in crash backtraces
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(SLINGSHOT_MODS)
        slingshot_add_mods(${exe} ${id})
    endif()
endforeach()

if(NOT built_games)
    message(WARNING "No game extracted: configure with -DGAME_APKS=/path/to/game.apk (a list for several games).")
else()
    message(STATUS "Games: ${built_games}")
endif()
