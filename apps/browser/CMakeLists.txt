# ═══════════════════════════════════════════ CMakeLists.txt (apps/browser) ══════════════════════════════
cmake_minimum_required(VERSION 3.20)
project(sabre-browser VERSION 0.1.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(BROWSER_MODE "SABRE" CACHE STRING "Browser build mode")
set_property(CACHE BROWSER_MODE PROPERTY STRINGS SABRE SCRAPER HEADLESS HEADFUL MINI PRIVATE)
message(STATUS "Building: ${BROWSER_MODE}")
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets WebEngineCore WebEngineWidgets WebChannel Sql Svg)
qt_standard_project_setup()
set(NOTHING_LANG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../packages/lang/langs" CACHE PATH "Path to language JSON files" FORCE)
set(PROFILE_BUILD_QT_LAYER ON CACHE BOOL "" FORCE)

add_subdirectory(../../packages/profile ${CMAKE_BINARY_DIR}/packages/profile)
add_subdirectory(../../packages/search ${CMAKE_BINARY_DIR}/packages/search)
add_subdirectory(../../packages/adblock ${CMAKE_BINARY_DIR}/packages/adblock)
include_directories(../../packages/lang/include)

qt_add_executable(sabre-browser
    main.cpp
    resources.qrc
    FingerprintController.h FingerprintController.cpp
    SearchController.h SearchController.cpp
    BrowserTab.h BrowserTab.cpp
    FormRecoveryController.h FormRecoveryController.cpp
    FossilCacheManager.h FossilCacheManager.cpp
    FossilSchemeHandler.h FossilSchemeHandler.cpp
    FossilBridge.h FossilBridge.cpp
    DownloadManager.h DownloadManager.cpp
    IncognitoWindow.h IncognitoWindow.cpp
    DownloadPanel.h DownloadPanel.cpp
    DownloadsPage.h  DownloadsPage.cpp
    SabreTabBar.h SabreTabBar.cpp
    NewTabPage.h NewTabPage.cpp NoInternetPage.h NoInternetPage.cpp
    OfflineGamePage.h OfflineGamePage.cpp ProfilePage.h ProfilePage.cpp
    SettingsPage.h SettingsPage.cpp NormalWindow.h NormalWindow.cpp
    AdblockInterceptor.h AdblockInterceptor.cpp
    AdblockPage.cpp AdblockPage.h
    ../../packages/lang/src/LangManager.cpp ../../packages/lang/include/LangManager.h
)

# ── Windows icon ──────────────────────────────────────────────────────────────
if(WIN32)
    target_sources(sabre-browser PRIVATE packaging/icons/sabre-browser.ico)
endif()

target_compile_definitions(sabre-browser PRIVATE
    SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    BROWSER_MODE="${BROWSER_MODE}"
    NOTHING_LANG_DIR="${NOTHING_LANG_DIR}"
)

target_include_directories(sabre-browser PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "../../packages/lang/include"
    "../../packages/adblock/include"
)

target_link_libraries(sabre-browser PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::WebEngineCore Qt6::WebEngineWidgets Qt6::WebChannel Qt6::Sql Qt6::Svg
    nothing-fingerprint-qt nothing-incognito nothing-search nothing-adblock
)

# ── Install binary ────────────────────────────────────────────────────────────
install(TARGETS sabre-browser BUNDLE DESTINATION . RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# ── Install Linux icons ───────────────────────────────────────────────────────
if(UNIX AND NOT APPLE)
    foreach(SIZE 16 32 48 128 256)
        install(FILES packaging/icons/${SIZE}x${SIZE}/sabre-browser.png
            DESTINATION share/icons/hicolor/${SIZE}x${SIZE}/apps
        )
    endforeach()

    install(FILES packaging/sabre-browser.desktop
        DESTINATION share/applications
    )

    install(CODE "
        execute_process(COMMAND gtk-update-icon-cache /usr/share/icons/hicolor/)
        execute_process(COMMAND update-desktop-database /usr/share/applications)
    ")
endif()

# ── Lang files ────────────────────────────────────────────────────────────────
if(EXISTS "${NOTHING_LANG_DIR}")
    add_custom_command(TARGET sabre-browser POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${NOTHING_LANG_DIR}"
            "$<TARGET_FILE_DIR:sabre-browser>/langs"
        COMMENT "Copying language files to build directory"
    )
endif()
