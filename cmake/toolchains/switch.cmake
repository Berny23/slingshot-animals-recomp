# SPDX-License-Identifier: GPL-3.0-or-later
# Nintendo Switch homebrew (devkitA64 + libnx), without devkitPro's CMake files:
#   cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/switch.cmake -DDEVKITPRO=<devkitpro> ...
# DEVKITPRO holds devkitA64/, libnx/, portlibs/switch/ (SDL2, mesa) and tools/bin/.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(NINTENDO_SWITCH TRUE)

if(NOT DEVKITPRO)
    if(DEFINED ENV{DEVKITPRO})
        set(DEVKITPRO "$ENV{DEVKITPRO}")
    else()
        set(DEVKITPRO "/opt/devkitpro")
    endif()
endif()
set(DEVKITPRO "${DEVKITPRO}" CACHE PATH "devkitPro installation (devkitA64, libnx, portlibs)")
set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES DEVKITPRO)

set(triplet aarch64-none-elf)
set(suffix "${CMAKE_HOST_EXECUTABLE_SUFFIX}")
set(CMAKE_C_COMPILER "${DEVKITPRO}/devkitA64/bin/${triplet}-gcc${suffix}")
set(CMAKE_CXX_COMPILER "${DEVKITPRO}/devkitA64/bin/${triplet}-g++${suffix}")
set(CMAKE_ASM_COMPILER "${CMAKE_C_COMPILER}")
set(CMAKE_AR "${DEVKITPRO}/devkitA64/bin/${triplet}-gcc-ar${suffix}" CACHE FILEPATH "")
set(CMAKE_RANLIB "${DEVKITPRO}/devkitA64/bin/${triplet}-gcc-ranlib${suffix}" CACHE FILEPATH "")
set(CMAKE_STRIP "${DEVKITPRO}/devkitA64/bin/${triplet}-strip${suffix}" CACHE FILEPATH "")

set(arch "-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -ftls-model=local-exec -fPIE")
set(CMAKE_C_FLAGS_INIT "${arch} -ffunction-sections -fdata-sections -D__SWITCH__")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_C_FLAGS_INIT}")
set(CMAKE_ASM_FLAGS_INIT "${arch}")
# libnx's switch.specs finds its linker script through $DEVKITPRO; this copy has the path.
file(READ "${DEVKITPRO}/libnx/switch.specs" specs)
string(REPLACE "%:getenv(DEVKITPRO /libnx/switch.ld)" "${DEVKITPRO}/libnx/switch.ld" specs "${specs}")
file(WRITE "${CMAKE_BINARY_DIR}/switch.specs" "${specs}")
# Paths in the specs file cannot have spaces (the builder passes a short path on Windows).
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "\"-specs=${CMAKE_BINARY_DIR}/switch.specs\" \"-L${DEVKITPRO}/portlibs/switch/lib\" \"-L${DEVKITPRO}/libnx/lib\"")
set(CMAKE_C_STANDARD_LIBRARIES "-lnx -lm")
set(CMAKE_CXX_STANDARD_LIBRARIES "-lnx -lm")
set(CMAKE_EXECUTABLE_SUFFIX ".elf")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS FALSE)  # libnx has static libraries only

set(CMAKE_FIND_ROOT_PATH "${DEVKITPRO}/portlibs/switch" "${DEVKITPRO}/libnx" "${DEVKITPRO}/devkitA64/${triplet}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
# No pkg-config: the host's (FreeType includes FindPkgConfig) knows only the
# host's libraries; on Windows it may be MSYS2's, under Wine even Linux's
# /bin/pkg-config. A false value that is not NOTFOUND skips the search.
set(PKG_CONFIG_EXECUTABLE OFF)
# CMake's Generic platform searches programs in /bin: on Windows that is no
# Windows directory (under Wine it is Linux's, whose programs cannot be started).
if(CMAKE_HOST_WIN32)
    list(APPEND CMAKE_SYSTEM_IGNORE_PATH /bin)
endif()
include_directories(SYSTEM "${DEVKITPRO}/libnx/include")
