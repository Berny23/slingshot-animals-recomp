# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(abrecomp
    src/main.cpp
    src/elf_image.cpp
    src/disasm.cpp
    src/analysis.cpp
    src/lifter.cpp
    src/lifter_vfp.cpp
    src/emitter.cpp
)
target_link_libraries(abrecomp PRIVATE slingshot::capstone)
target_compile_options(abrecomp PRIVATE -Wall -Wextra)
# Host tools in one place (SLINGSHOT_HOST_TOOLS of a cross build).
set_target_properties(abrecomp PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/host-tools")
