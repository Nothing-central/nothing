cmake_minimum_required(VERSION 3.20)
project(sabre-browser VERSION 0.1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BROWSER_MODE "SABRE" CACHE STRING "Browser build mode")
set_property(CACHE BROWSER_MODE PROPERTY STRINGS
    SABRE SCRAPER HEADLESS HEADFUL MINI PRIVATE)

message(STATUS "Building: ${BROWSER_MODE}")

# ─── Qt6 ────────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS
    Core Gui Qml Quick WebEngineQuick)

qt_standard_project_setup()

# ─── Shared packages ────────────────────────────────────────────
add_subdirectory(../../packages/lang ${CMAKE_BINARY_DIR}/packages/lang)

set(PROFILE_BUILD_QT_LAYER ON CACHE BOOL "" FORCE)
add_subdirectory(../../packages/profile ${CMAKE_BINARY_DIR}/packages/profile)

# ─── Sources ────────────────────────────────────────────────────
qt_add_executable(sabre-browser
    main.cpp
    FingerprintController.h
    FingerprintController.cpp
    ../../packages/lang/src/LangManager.cpp
    ../../packages/lang/include/LangManager.h
)

target_compile_definitions(sabre-browser PRIVATE
    SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    BROWSER_MODE="${BROWSER_MODE}"
    NOTHING_LANG_DIR="${NOTHING_LANG_DIR}"
)

# ─── Link ───────────────────────────────────────────────────────
target_link_libraries(sabre-browser PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::WebEngineQuick
    nothing-lang
    nothing-fingerprint-qt
    nothing-incognito
)

# ─── QML ────────────────────────────────────────────────────────
qt_add_qml_module(sabre-browser
    URI SabreBrowser
    VERSION 1.0
    QML_FILES
        ui/pages/SplashScreen.qml
        ui/pages/NormalWindow.qml
        ui/pages/IncognitoWindow.qml
        ui/components/Toolbar.qml
        ui/components/TabBar.qml
        ui/components/AddressBar.qml
        ui/components/NewTabPage.qml
        ui/components/TabWebView.qml
)

# ─── Future packages ────────────────────────────────────────────
# add_subdirectory(../../packages/adblock ${CMAKE_BINARY_DIR}/packages/adblock)
# add_subdirectory(../../packages/storage ${CMAKE_BINARY_DIR}/packages/storage)
# add_subdirectory(../../packages/devtools ${CMAKE_BINARY_DIR}/packages/devtools)
# target_link_libraries(sabre-browser PRIVATE nothing-adblock ...)

install(TARGETS sabre-browser
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)