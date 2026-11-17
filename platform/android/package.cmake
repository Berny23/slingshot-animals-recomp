# SPDX-License-Identifier: GPL-3.0-or-later
# Builds a game's signed APK (cmake -P, called by games/CMakeLists.txt):
# SDL's Java classes (javac, d8), manifest and launcher icons (aapt2), native
# libraries and game files (slingshot-apk pack), signature (apksigner).
# Variables: SDK BUILD_TOOLS PLATFORM JDK SDL_JAVA MANIFEST GAME_DIR ABI LIBS
#            WORK OUT KEYSTORE APK_TOOL MIN_SDK TARGET_SDK
cmake_minimum_required(VERSION 3.28)

set(bt "${SDK}/build-tools/${BUILD_TOOLS}")
set(jar "${SDK}/platforms/${PLATFORM}/android.jar")
if(CMAKE_HOST_WIN32)
    set(exe ".exe")
    set(bat ".bat")
endif()
set(ENV{JAVA_HOME} "${JDK}")

function(run)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE r OUTPUT_VARIABLE out ERROR_VARIABLE out)
    if(NOT r EQUAL 0)
        list(GET ARGN 0 tool)
        message(FATAL_ERROR "${tool} failed:\n${out}")
    endif()
endfunction()

# classes.dex: shared by all games, rebuilt when SDL's sources change.
get_filename_component(java_dir "${WORK}/../java" ABSOLUTE)
file(GLOB_RECURSE sources "${SDL_JAVA}/*.java")
set(dex "${java_dir}/classes.dex")
set(rebuild FALSE)
if(NOT EXISTS "${dex}")
    set(rebuild TRUE)
else()
    foreach(s IN LISTS sources)
        if("${s}" IS_NEWER_THAN "${dex}")
            set(rebuild TRUE)
        endif()
    endforeach()
endif()
if(rebuild)
    file(REMOVE_RECURSE "${java_dir}")
    file(MAKE_DIRECTORY "${java_dir}/classes")
    run("${JDK}/bin/javac${exe}" -nowarn -source 8 -target 8 -bootclasspath "${jar}" -d "${java_dir}/classes" ${sources})
    file(GLOB_RECURSE classes "${java_dir}/classes/*.class")
    run("${bt}/d8${bat}" --release --min-api ${MIN_SDK} --lib "${jar}" --output "${java_dir}" ${classes})
endif()

# Launcher icons: icons/<dpi>.png -> res/mipmap-<density>/ic_launcher.png
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/res")
set(buckets 120 ldpi 160 mdpi 240 hdpi 320 xhdpi 480 xxhdpi 640 xxxhdpi)
file(GLOB icons "${GAME_DIR}/icons/*.png")
foreach(icon IN LISTS icons)
    get_filename_component(dpi "${icon}" NAME_WE)
    list(FIND buckets ${dpi} i)
    if(i GREATER_EQUAL 0)
        math(EXPR i "${i} + 1")
        list(GET buckets ${i} name)
        file(MAKE_DIRECTORY "${WORK}/res/mipmap-${name}")
        file(COPY_FILE "${icon}" "${WORK}/res/mipmap-${name}/ic_launcher.png")
    endif()
endforeach()
run("${bt}/aapt2${exe}" compile --dir "${WORK}/res" -o "${WORK}/res.zip")
run("${bt}/aapt2${exe}" link -o "${WORK}/base.apk" -I "${jar}" --manifest "${MANIFEST}"
    --min-sdk-version ${MIN_SDK} --target-sdk-version ${TARGET_SDK} "${WORK}/res.zip")

run("${APK_TOOL}" pack "${WORK}/unsigned.apk" "${WORK}/base.apk" "${dex}" ${ABI} "${GAME_DIR}" ${LIBS})

# One key per keystore: an installed game updates only with the same signature.
if(NOT EXISTS "${KEYSTORE}")
    get_filename_component(dir "${KEYSTORE}" DIRECTORY)
    file(MAKE_DIRECTORY "${dir}")
    run("${JDK}/bin/keytool${exe}" -genkeypair -keystore "${KEYSTORE}" -storepass slingshot -keypass slingshot
        -alias slingshot -keyalg RSA -keysize 3072 -validity 36500 -dname "CN=slingshot-animals-recomp")
endif()
run("${bt}/apksigner${bat}" sign --v4-signing-enabled false --ks "${KEYSTORE}" --ks-pass pass:slingshot --ks-key-alias slingshot
    --out "${OUT}" "${WORK}/unsigned.apk")
file(REMOVE_RECURSE "${WORK}")
message(STATUS "Wrote ${OUT}")
