cmake_minimum_required(VERSION 3.20)
project(nothing-profile VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── fingerprint core (no Qt) ───────────────────────────────────
# Pure C++ — builds identity data. Everything downstream reads from this.
set(FINGERPRINT_SOURCES
    fingerprint/src/navigator_profile.cpp
    fingerprint/src/screen_profile.cpp
    fingerprint/src/webgl_profile.cpp
    fingerprint/src/font_visibility.cpp
    fingerprint/src/canvas_interceptor.cpp
    fingerprint/src/audio_interceptor.cpp
    fingerprint/src/site_key.cpp
)

set(FINGERPRINT_HEADERS
    fingerprint/include/navigator_profile.h
    fingerprint/include/screen_profile.h
    fingerprint/include/webgl_profile.h
    fingerprint/include/audio_profile.h
    fingerprint/include/font_visibility.h
    fingerprint/include/font_hooks.h
    fingerprint/include/generated_fonts_direct.h
    fingerprint/include/generated_fonts.h    # header only — no .cpp needed
    fingerprint/include/fp_target.h          # header only — no .cpp needed
    fingerprint/include/fp_dispatcher.h      # header only — no .cpp needed
    fingerprint/include/canvas_interceptor.h
    fingerprint/include/audio_interceptor.h
    fingerprint/include/site_key.h
    fingerprint/src/sha256.h                 # header only — no .cpp needed
)

# ─── fingerprint library target ─────────────────────────────────
# Builds nothing-fingerprint as a static library.
# identity/ links against this; qt/ links against this too.
add_library(nothing-fingerprint STATIC ${FINGERPRINT_SOURCES})

target_include_directories(nothing-fingerprint PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/fingerprint/include
)

# ─── identity ────────────────────────────────────────────────────
# Owns IdentityBundle / IdentityManager — depends on fingerprint core only.
set(IDENTITY_SOURCES
    identity/src/identity_manager.cpp
)

set(IDENTITY_HEADERS
    identity/include/identity_bundle.h
    identity/include/identity_manager.h
)

add_library(nothing-identity STATIC ${IDENTITY_SOURCES})

target_include_directories(nothing-identity PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/identity/include
)

target_link_libraries(nothing-identity PUBLIC nothing-fingerprint)

# ─── incognito ───────────────────────────────────────────────────
# Header-only policy wrapper over identity/ — no .cpp, no separate library,
# just needs identity/include on its include path.
set(INCOGNITO_HEADERS
    incognito/include/incognito_session.h    # header only — no .cpp needed
)

add_library(nothing-incognito INTERFACE)

target_include_directories(nothing-incognito INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/incognito/include
)

target_link_libraries(nothing-incognito INTERFACE nothing-identity)

# ─── Qt glue (only built when Qt6 is available/requested) ───────
# Not compiled by default — apps/browser or apps/scraper's own CMakeLists
# should find_package(Qt6 ...) and enable this, since this package itself
# shouldn't hard-require Qt just to build the pure-C++ core above.
option(PROFILE_BUILD_QT_LAYER "Build the Qt6-dependent injection layer" OFF)

if(PROFILE_BUILD_QT_LAYER)
    find_package(Qt6 REQUIRED COMPONENTS Core)

    set(QT_LAYER_SOURCES
        fingerprint/qt/profile_to_json.cpp
        fingerprint/qt/FingerprintSpoofer.cpp
    )

    set(QT_LAYER_HEADERS
        fingerprint/qt/profile_to_json.h
        fingerprint/qt/FingerprintSpoofer.h
    )

    add_library(nothing-fingerprint-qt STATIC ${QT_LAYER_SOURCES})

    target_include_directories(nothing-fingerprint-qt PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/fingerprint/qt
    )

    target_link_libraries(nothing-fingerprint-qt PUBLIC
        nothing-fingerprint
        nothing-identity
        Qt6::Core
    )
endif()

# ─── Test binary ─────────────────────────────────────────────────
# Only built when testing, not when used as a dependency.
# No Qt required — this is the standalone JSON identity report.
option(PROFILE_BUILD_TESTS "Build profile/fingerprint tests" OFF)

if(PROFILE_BUILD_TESTS)
    add_executable(fingerprint_report fingerprint/test/fingerprint_report.cpp)
    target_link_libraries(fingerprint_report PRIVATE nothing-fingerprint)
endif()

# ─── Roadmap ─────────────────────────────────────────────────────
# Files to add in future sessions:
#   fingerprint/src/font_blink_hook.cpp — native font visibility enforcement,
#     IF a Blink/Chromium hook point is ever found (currently a known gap,
#     see docs.md — JS injection can't reach document.fonts.check()/local()).
#   fingerprint/include/font_blink_hook.h
#   identity/src/identity_persistence.cpp — optional: persist "default"
#     identity across app restarts (deliberately NOT done for incognito).
#   identity/include/identity_persistence.h