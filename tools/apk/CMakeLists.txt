# SPDX-License-Identifier: GPL-3.0-or-later
# APK reading and writing (zip, app icon), shared by the build and the builder app.
add_library(slingshot_apk STATIC
    src/android_res.cpp
    src/extract.cpp
    src/image.cpp
    src/nx_elf.cpp
    src/pack.cpp
    src/sha256.cpp
    src/zip.cpp)
target_include_directories(slingshot_apk PUBLIC src)
target_link_libraries(slingshot_apk PUBLIC slingshot::zlib)
target_compile_features(slingshot_apk PUBLIC cxx_std_20)
target_compile_options(slingshot_apk PRIVATE -Wall -Wextra)

add_executable(slingshot-apk src/main.cpp)
target_link_libraries(slingshot-apk PRIVATE slingshot_apk)
# Host tools in one place (SLINGSHOT_HOST_TOOLS of a cross build).
set_target_properties(slingshot-apk PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/host-tools")
