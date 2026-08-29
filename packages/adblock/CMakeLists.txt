cmake_minimum_required(VERSION 3.20)
project(nothing-adblock VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── Source files ───────────────────────────────────────────────
# Add every new .cpp file here as you create it
set(ADBLOCK_SOURCES
    src/tokenizer.cpp
    src/token_selector.cpp
    src/network_filter.cpp
    src/request.cpp
    src/blocker.cpp
)

# ─── Header files ───────────────────────────────────────────────
# Add every new .h file here as you create it
set(ADBLOCK_HEADERS
    include/tokenizer.h
    include/token_selector.h
    include/flat_multimap.h       # header only — no .cpp needed
    include/network_filter.h
    include/request.h
    include/blocker.h
)

# ─── Library target ─────────────────────────────────────────────
# Builds nothing-adblock as a static library
# Other apps (browser, scraper, private) link against this
add_library(nothing-adblock STATIC ${ADBLOCK_SOURCES})

target_include_directories(nothing-adblock PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ─── Test binary ────────────────────────────────────────────────
# Only built when testing, not when used as a dependency
option(ADBLOCK_BUILD_TESTS "Build adblock tests" OFF)

if(ADBLOCK_BUILD_TESTS)
    add_executable(test_adblock src/test_tokenizer.cpp)
    target_link_libraries(test_adblock PRIVATE nothing-adblock)
endif()

# ─── Roadmap ────────────────────────────────────────────────────
# Files to add in future sessions:
#
# src/regex_manager.cpp        — regex filter support (RE2)
# include/regex_manager.h
#
# src/cosmetic_filter.cpp      — DOM element hiding
# include/cosmetic_filter.h
#
# src/cosmetic_cache.cpp       — cosmetic filter lookup cache
# include/cosmetic_cache.h
#
# src/resource_storage.cpp     — scriptlet + redirect resources
# include/resource_storage.h
#
# src/filter_list_updater.cpp  — auto-update filter lists
# include/filter_list_updater.h
