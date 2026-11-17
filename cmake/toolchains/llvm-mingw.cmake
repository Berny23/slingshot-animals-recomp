# SPDX-License-Identifier: GPL-3.0-or-later
# Cross build for Windows with llvm-mingw (https://github.com/mstorsjo/llvm-mingw):
#   cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/llvm-mingw.cmake -DLLVM_MINGW=<dir> [-DLLVM_MINGW_ARCH=aarch64] ...
set(CMAKE_SYSTEM_NAME Windows)
set(LLVM_MINGW "" CACHE PATH "llvm-mingw directory")
set(LLVM_MINGW_ARCH "x86_64" CACHE STRING "x86_64 or aarch64")
set(CMAKE_SYSTEM_PROCESSOR ${LLVM_MINGW_ARCH})
set(prefix "${LLVM_MINGW}/bin/${LLVM_MINGW_ARCH}-w64-mingw32-")
set(host_exe "")
if(CMAKE_HOST_WIN32)
    set(host_exe ".exe")
endif()
set(CMAKE_C_COMPILER "${prefix}clang${host_exe}")
set(CMAKE_CXX_COMPILER "${prefix}clang++${host_exe}")
set(CMAKE_RC_COMPILER "${prefix}windres${host_exe}")
# Other roots (e.g. a Qt for Windows) can be given with -DCMAKE_FIND_ROOT_PATH.
list(APPEND CMAKE_FIND_ROOT_PATH "${LLVM_MINGW}/${LLVM_MINGW_ARCH}-w64-mingw32")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
