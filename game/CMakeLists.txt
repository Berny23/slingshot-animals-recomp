# SPDX-License-Identifier: GPL-3.0-or-later
# Recompiled game code: generated from the user's library at build time.
file(SHA256 "${GAME_LIB}" GAME_LIB_SHA256)
set(GEN_DIR "${CMAKE_BINARY_DIR}/generated")
set(GEN_FILE_COUNT 128)  # must match EmitOptions::file_count

set(GEN_SOURCES "${GEN_DIR}/table.cpp")
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
    list(APPEND GEN_SOURCES "${GEN_DIR}/funcs_${n}.cpp")
endforeach()

add_custom_command(
    OUTPUT ${GEN_SOURCES} "${GEN_DIR}/funcs.hpp"
    # The asset loaders accept plain files too (runtime/hle/fusion/script_loader.cpp).
    COMMAND abrecomp emit "${GAME_LIB}" "${GEN_DIR}" --sha256 "${GAME_LIB_SHA256}"
            --wrap 0x1313e4=fusion_load_asset --wrap 0x1319b8=fusion_load_config --wrap 0x5ae4a4=fusion_load_script
    DEPENDS abrecomp "${GAME_LIB}"
    COMMENT "Recompiling libAngryBirdsClassic.so"
    VERBATIM)

add_library(recomp_game_code OBJECT ${GEN_SOURCES})
target_link_libraries(recomp_game_code PRIVATE recomp_runtime)
# Guest floating point must not be contracted into FMAs; guest memory is
# accessed through byte copies, but keep the optimiser conservative anyway.
target_compile_options(recomp_game_code PRIVATE -O2 -ffp-contract=off -fno-strict-aliasing -w)
