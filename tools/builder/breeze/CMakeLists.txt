# SPDX-License-Identifier: GPL-3.0-or-later
# KDE's Breeze widget style built into the builder (the same look everywhere),
# without KDE Frameworks: KColorUtils and KColorScheme are KDE's own sources,
# compat/ stands in for the rest (configuration, window effects, icons).
include(FetchContent)
FetchContent_Declare(breeze
    URL https://download.kde.org/stable/plasma/6.8.3/breeze-6.8.3.tar.xz
    URL_HASH SHA256=1d47a61f9f397f212d7a23d7ed7a7289b026757a050610304bb945dc067937e2
    SOURCE_SUBDIR none)
FetchContent_Declare(kguiaddons
    URL https://download.kde.org/stable/frameworks/6.32/kguiaddons-6.32.0.tar.xz
    URL_HASH SHA256=ea1bf879d181e498d1786451234a0e58827f35cfb4c667661412cd551741e81a
    SOURCE_SUBDIR none)
FetchContent_Declare(kcolorscheme
    URL https://download.kde.org/stable/frameworks/6.32/kcolorscheme-6.32.0.tar.xz
    URL_HASH SHA256=235c73afa46669c41cbe2e3a14c5a6ae1f8ad8cbb386b3f65261f65449d2b915
    SOURCE_SUBDIR none)
FetchContent_MakeAvailable(breeze kguiaddons kcolorscheme)

set(style ${breeze_SOURCE_DIR}/kstyle)
set(sources
    ${kguiaddons_SOURCE_DIR}/src/colors/kcolorutils.cpp
    ${kguiaddons_SOURCE_DIR}/src/colors/kcolorspaces.cpp
    ${kcolorscheme_SOURCE_DIR}/src/kcolorscheme.cpp
    ${kcolorscheme_SOURCE_DIR}/src/kstatefulbrush.cpp
    ${breeze_SOURCE_DIR}/libbreezecommon/breezeboxshadowrenderer.cpp
    compat/kconfig.cpp
    compat/breezestyleconfigdata.cpp
    compat/kcolorschememanager.cpp
    breeze_style.cpp)
foreach(f IN ITEMS
        animations/breezeanimation animations/breezeanimations animations/breezeanimationdata
        animations/breezebaseengine animations/breezebusyindicatordata animations/breezebusyindicatorengine
        animations/breezedialdata animations/breezedialengine animations/breezeenabledata
        animations/breezegenericdata animations/breezeheaderviewdata animations/breezeheaderviewengine
        animations/breezescrollbardata animations/breezescrollbarengine animations/breezespinboxengine
        animations/breezespinboxdata animations/breezestackedwidgetdata animations/breezestackedwidgetengine
        animations/breezetabbarengine animations/breezetabbardata animations/breezetoolboxengine
        animations/breezetransitiondata animations/breezetransitionwidget animations/breezewidgetstateengine
        animations/breezewidgetstatedata debug/breezewidgetexplorer
        breezeaddeventfilter breezeblurhelper breezeframeshadow breezehelper breezemdiwindowshadow
        breezemnemonics breezepropertynames breezeshadowhelper breezesplitterproxy breezestyle
        breezetileset breezewindowmanager breezetoolsareamanager)
    list(APPEND sources ${style}/${f}.cpp)
endforeach()

# The colour schemes for use_breeze()
foreach(scheme BreezeLight BreezeDark)
    configure_file(${breeze_SOURCE_DIR}/colors/${scheme}.colors ${CMAKE_CURRENT_BINARY_DIR}/${scheme}.colors COPYONLY)
endforeach()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/breeze.qrc
    "<RCC><qresource prefix=\"/breeze\"><file>BreezeLight.colors</file><file>BreezeDark.colors</file></qresource></RCC>\n")

add_library(builder_breeze STATIC ${sources} ${CMAKE_CURRENT_BINARY_DIR}/breeze.qrc)
set_target_properties(builder_breeze PROPERTIES AUTOMOC ON AUTORCC ON)
target_include_directories(builder_breeze
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} compat ${kcolorscheme_SOURCE_DIR}/src ${kguiaddons_SOURCE_DIR}/src/colors
    PRIVATE ${style} ${style}/animations ${style}/debug ${breeze_SOURCE_DIR}/libbreezecommon)
target_compile_definitions(builder_breeze PRIVATE TRANSLATION_DOMAIN="breeze_style_qt")
target_link_libraries(builder_breeze PUBLIC Qt6::Widgets)
