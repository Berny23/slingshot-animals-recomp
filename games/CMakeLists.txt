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

# Android: bin/slingshot-<id>.apk, signed with SLINGSHOT_ANDROID_KEYSTORE
# (platform/android/package.cmake). Uses the variables of the game's loop below.
set(SLINGSHOT_ANDROID_SDK "" CACHE PATH "Android SDK with build-tools and platforms (builds APKs)")
set(SLINGSHOT_JDK "$ENV{JAVA_HOME}" CACHE PATH "Java development kit for the Android packaging tools")
set(SLINGSHOT_ANDROID_KEYSTORE "${CMAKE_BINARY_DIR}/android.keystore" CACHE FILEPATH "Key that signs the APKs (created if missing)")
function(slingshot_android_apk id)
    file(GLOB tools LIST_DIRECTORIES true "${SLINGSHOT_ANDROID_SDK}/build-tools/*")
    file(GLOB platforms LIST_DIRECTORIES true "${SLINGSHOT_ANDROID_SDK}/platforms/android-*")
    if(NOT tools OR NOT platforms)
        message(FATAL_ERROR "SLINGSHOT_ANDROID_SDK needs build-tools and platforms: ${SLINGSHOT_ANDROID_SDK}")
    endif()
    list(SORT tools COMPARE NATURAL ORDER DESCENDING)
    list(SORT platforms COMPARE NATURAL ORDER DESCENDING)
    list(GET tools 0 bt)
    list(GET platforms 0 platform)
    get_filename_component(bt "${bt}" NAME)
    get_filename_component(platform "${platform}" NAME)
    string(REGEX REPLACE "android-([0-9]+).*" "\\1" target_sdk "${platform}")
    # Not the original package: both can be installed.
    set(APK_PACKAGE "${GAME_PACKAGE}.recomp")
    string(REGEX MATCH "build ([0-9]+)" m "${GAME_VERSION_FULL}")
    set(APK_VERSION_CODE "${CMAKE_MATCH_1}")
    if(NOT APK_VERSION_CODE)
        set(APK_VERSION_CODE 1)
    endif()
    set(manifest "${CMAKE_CURRENT_BINARY_DIR}/${id}/AndroidManifest.xml")
    configure_file("${CMAKE_SOURCE_DIR}/platform/android/AndroidManifest.xml.in" "${manifest}" @ONLY)
    set(apk "${CMAKE_BINARY_DIR}/bin/slingshot-${id}.apk")
    add_custom_command(
        OUTPUT "${apk}"
        COMMAND "${CMAKE_COMMAND}" -DSDK=${SLINGSHOT_ANDROID_SDK} -DBUILD_TOOLS=${bt} -DPLATFORM=${platform}
                -DJDK=${SLINGSHOT_JDK} -DSDL_JAVA=${sdl3_SOURCE_DIR}/android-project/app/src/main/java
                -DMANIFEST=${manifest} -DGAME_DIR=${GAME_DIR} -DABI=${ANDROID_ABI}
                "-DLIBS=$<TARGET_FILE:slingshot-${id}>;$<TARGET_FILE:SDL3::SDL3-shared>"
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/${id}/apk -DOUT=${apk} -DKEYSTORE=${SLINGSHOT_ANDROID_KEYSTORE}
                -DAPK_TOOL=$<TARGET_FILE:slingshot-apk> -DMIN_SDK=${ANDROID_NATIVE_API_LEVEL} -DTARGET_SDK=${target_sdk}
                -P "${CMAKE_SOURCE_DIR}/platform/android/package.cmake"
        DEPENDS slingshot-${id} SDL3::SDL3-shared slingshot-apk "${manifest}" ${GAME_FILES}
                "${CMAKE_SOURCE_DIR}/platform/android/package.cmake"
        COMMENT "Packaging ${GAME_TITLE} for Android"
        VERBATIM)
    add_custom_target(slingshot-${id}-apk ALL DEPENDS "${apk}")
endfunction()

# Nintendo Switch: bin/slingshot-<id>.nro, a homebrew with the game's files as its romfs.
function(slingshot_switch_nro id)
    set(tools "${DEVKITPRO}/tools/bin")
    # Only the library and assets/ (the game directory holds the build's files too).
    set(work "${CMAKE_BINARY_DIR}/switch/${id}")
    set(nro "${CMAKE_BINARY_DIR}/bin/slingshot-${id}.nro")
    add_custom_command(
        OUTPUT "${nro}"
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${work}/romfs"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${work}/romfs"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different "${GAME_DIR}/lib" "${work}/romfs/lib"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different "${GAME_DIR}/assets" "${work}/romfs/assets"
        COMMAND $<TARGET_FILE:slingshot-apk> nro-icon "${GAME_DIR}/icon.png" "${work}/icon.jpg"
        COMMAND "${tools}/nacptool${CMAKE_HOST_EXECUTABLE_SUFFIX}" --create "${GAME_TITLE}" slingshot-animals-recomp
                "${GAME_VERSION}" "${work}/control.nacp"
        COMMAND $<TARGET_FILE:slingshot-apk> nx-elf $<TARGET_FILE:slingshot-${id}> "${work}/slingshot-${id}.elf"
        COMMAND "${tools}/elf2nro${CMAKE_HOST_EXECUTABLE_SUFFIX}" "${work}/slingshot-${id}.elf" "${nro}"
                "--icon=${work}/icon.jpg" "--nacp=${work}/control.nacp" "--romfsdir=${work}/romfs"
        DEPENDS slingshot-${id} slingshot-apk ${GAME_FILES}
        COMMENT "Packaging ${GAME_TITLE} for the Nintendo Switch"
        VERBATIM)
    add_custom_target(slingshot-${id}-nro ALL DEPENDS "${nro}")
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
    set(GAME_VERSION_FULL "${GAME_VERSION}")
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
    if(SLINGSHOT_MODS)
        set(GAME_DEFAULT_MODS "${SLINGSHOT_DEFAULT_MODS_LIST}")
    else()
        set(GAME_DEFAULT_MODS "")
    endif()
    set(GAME_DIR "${GAME_ROOT}/${id}")
    set(game_lib "${GAME_DIR}/lib/lib${GAME_LIBRARY}.so")
    if(NOT DEFINED GAME_APK_${id})
        continue()
    endif()
    list(APPEND built_games "${id}")

    # The library, assets/ and icons from the APK (.complete marks the end).
    add_custom_command(
        OUTPUT "${GAME_DIR}/.complete"
        BYPRODUCTS "${game_lib}" "${GAME_DIR}/icon.png" "${GAME_DIR}/icon.ico"
        COMMAND $<TARGET_FILE:slingshot-apk> extract "${GAME_APK_${id}}" "${GAME_DIR}" "${GAME_LIBRARY}" "${lib_sha256}"
        DEPENDS slingshot-apk "${GAME_APK_${id}}"
        COMMENT "Extracting ${GAME_TITLE} from its APK"
        VERBATIM)

    # Optional content the game once downloaded, into assets/: Star Wars II's
    # files/assets_service/ (GAME_EXTRA_starwars2, mods/offline_assets).
    set(extra_dir "${GAME_DIR}/assets/assets_service")
    set(GAME_FILES "${GAME_DIR}/.complete")
    if(id STREQUAL "starwars2" AND NOT GAME_EXTRA_${id} STREQUAL "")
        set(extra "${GAME_EXTRA_${id}}")
        set(extra_depends "${extra}")
        if(IS_DIRECTORY "${extra}")
            file(GLOB_RECURSE extra_depends CONFIGURE_DEPENDS "${extra}/*")
        endif()
        add_custom_command(
            OUTPUT "${GAME_DIR}/.extra"
            COMMAND $<TARGET_FILE:slingshot-apk> assets-service "${extra}" "${extra_dir}"
            COMMAND "${CMAKE_COMMAND}" -E touch "${GAME_DIR}/.extra"
            DEPENDS slingshot-apk "${GAME_DIR}/.complete" ${extra_depends}
            COMMENT "Adding the extra data of ${GAME_TITLE}"
            VERBATIM)
        list(APPEND GAME_FILES "${GAME_DIR}/.extra")
        add_custom_target(slingshot-${id}-extra ALL DEPENDS "${GAME_DIR}/.extra")
    else()
        file(REMOVE_RECURSE "${extra_dir}" "${GAME_DIR}/.extra")
    endif()

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
        COMMAND $<TARGET_FILE:abrecomp> emit "${game_lib}" "${gen_dir}" --sha256 "${lib_sha256}" ${wraps}
        DEPENDS abrecomp "${GAME_DIR}/.complete" "${game_lib}" "${game_file}"
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
    set(sources "${CMAKE_CURRENT_BINARY_DIR}/${id}/game_info.cpp" "${CMAKE_SOURCE_DIR}/platform/sdl/main.cpp")
    if(ANDROID)
        # SDL's activity loads libmain.so.
        add_library(${exe} SHARED ${sources})
        set_target_properties(${exe} PROPERTIES OUTPUT_NAME main LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/${id}")
        target_link_options(${exe} PRIVATE $<$<NOT:$<CONFIG:Debug>>:-s>)  # no debug info inside the APK
    else()
        add_executable(${exe} ${sources})
        set_target_properties(${exe} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
        if(NOT NINTENDO_SWITCH)
            set_target_properties(${exe} PROPERTIES ENABLE_EXPORTS ON)  # symbol names in crash backtraces
        endif()
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            # Keep the static libraries' symbols (HarfBuzz, FreeType, SDL3) private: exported, they
            # would replace the system's copies in libraries such as GTK that libdecor loads.
            target_link_options(${exe} PRIVATE -Wl,--exclude-libs,ALL)
        endif()
    endif()
    target_link_libraries(${exe} PRIVATE slingshot_host recomp_runtime recomp_game_code_${id})
    if(TARGET slingshot-${id}-extra)
        add_dependencies(${exe} slingshot-${id}-extra)
    endif()
    if(SLINGSHOT_MODS)
        slingshot_add_mods(${exe} ${id})
    endif()
    if(WIN32)
        # Icon, version information and the UTF-8 code page (platform/windows); no console window.
        string(REGEX MATCHALL "[0-9]+" parts "${GAME_VERSION}")
        list(APPEND parts 0 0 0 0)
        list(SUBLIST parts 0 4 parts)
        string(REPLACE ";" "," GAME_VERSION_COMMAS "${parts}")
        set(GAME_ICON "${GAME_DIR}/icon.ico")
        set(GAME_MANIFEST "${CMAKE_SOURCE_DIR}/platform/windows/game.manifest")
        set(rc "${CMAKE_CURRENT_BINARY_DIR}/${id}/game.rc")
        configure_file("${CMAKE_SOURCE_DIR}/platform/windows/game.rc.in" "${rc}" @ONLY)
        set_source_files_properties("${rc}" PROPERTIES OBJECT_DEPENDS "${GAME_DIR}/.complete;${GAME_MANIFEST}")
        target_sources(${exe} PRIVATE "${rc}")
        set_target_properties(${exe} PROPERTIES WIN32_EXECUTABLE ON)
        target_link_options(${exe} PRIVATE -static)
    endif()
    if(ANDROID AND SLINGSHOT_ANDROID_SDK)
        slingshot_android_apk(${id})
    endif()
    if(NINTENDO_SWITCH)
        slingshot_switch_nro(${id})
    endif()
endforeach()

if(NOT built_games)
    message(WARNING "No game: configure with -DGAME_APKS=/path/to/game.apk (a list for several games).")
else()
    message(STATUS "Games: ${built_games}")
endif()
