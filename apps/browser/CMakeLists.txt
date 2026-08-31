cmake_minimum_required(VERSION 3.20)
project(sabre-browser VERSION 0.1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── Build mode ─────────────────────────────────────────────────
# Set via -DBROWSER_MODE=SABRE|SCRAPER|HEADLESS|HEADFUL|MINI|PRIVATE
set(BROWSER_MODE "SABRE" CACHE STRING "Browser build mode")
set_property(CACHE BROWSER_MODE PROPERTY STRINGS
    SABRE SCRAPER HEADLESS HEADFUL MINI PRIVATE)

message(STATUS "Building: ${BROWSER_MODE}")

# ─── Qt6 ────────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Qml
    Quick
    WebEngineQuick
)

qt_standard_project_setup()

# ─── Sources ────────────────────────────────────────────────────
set(SOURCES
    main.cpp
)

# ─── Executable ─────────────────────────────────────────────────
qt_add_executable(sabre-browser ${SOURCES})

# ─── Pass source dir to C++ so QML can be found at runtime ──────
target_compile_definitions(sabre-browser PRIVATE
    SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    BROWSER_MODE="${BROWSER_MODE}"
)

# ─── Link Qt ────────────────────────────────────────────────────
target_link_libraries(sabre-browser PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::WebEngineQuick
)

# ─── QML files (so Qt knows about them) ─────────────────────────
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
)

# ─── Assets ─────────────────────────────────────────────────────
# Fonts, icons, images are loaded at runtime from SOURCE_DIR
# No need to embed them for now

# ─── Install ────────────────────────────────────────────────────
install(TARGETS sabre-browser
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# ─── Future packages to link ────────────────────────────────────
# add_subdirectory(../../packages/adblock adblock)
# add_subdirectory(../../packages/profile profile)
# add_subdirectory(../../packages/storage storage)
# add_subdirectory(../../packages/devtools devtools)
# target_link_libraries(sabre-browser PRIVATE nothing-adblock)
