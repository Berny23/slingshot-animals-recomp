# SPDX-License-Identifier: GPL-3.0-or-later
find_package(PkgConfig REQUIRED)
pkg_check_modules(CAPSTONE REQUIRED IMPORTED_TARGET capstone>=5)

add_executable(abrecomp
    src/main.cpp
    src/elf_image.cpp
    src/disasm.cpp
    src/analysis.cpp
    src/lifter.cpp
    src/lifter_vfp.cpp
    src/emitter.cpp
)
target_link_libraries(abrecomp PRIVATE PkgConfig::CAPSTONE)
target_compile_options(abrecomp PRIVATE -Wall -Wextra)
