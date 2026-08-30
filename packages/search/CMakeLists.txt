cmake_minimum_required(VERSION 3.20)
project(nothing-search VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── Source files ───────────────────────────────────────────────
set(SEARCH_SOURCES
    src/search_engine.cpp
    src/search_ipc_handlers.cpp
)

# ─── Header files ───────────────────────────────────────────────
set(SEARCH_HEADERS
    include/search_engine.h
    include/search_engine_registry.h   # header only — no .cpp needed
    include/search_engine_manager.h    # header only — no .cpp needed
    include/search_ipc_handlers.h
    include/search_suggest_cache.h     # header only — no .cpp needed
)

# ─── Library target ─────────────────────────────────────────────
add_library(nothing-search STATIC ${SEARCH_SOURCES})

target_include_directories(nothing-search PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ─── Test binary ────────────────────────────────────────────────
option(SEARCH_BUILD_TESTS "Build search tests" OFF)

if(SEARCH_BUILD_TESTS)
    add_executable(test_search src/test_search.cpp)
    target_link_libraries(test_search PRIVATE nothing-search)
endif()

# ─── Roadmap ────────────────────────────────────────────────────
# Files to add in future sessions:
#   src/search_ipc_dispatcher_wiring.cpp — replaces the placeholder body of
#     RegisterSearchHandlers() with the actual piggycpp dispatcher call, once
#     confirmed how PiggyFind/PiggyProvide register their command handlers.
#   src/search_json.cpp — replace ExtractStringField's ad-hoc parsing with
#     piggycpp's existing JSON library, if one is already in use elsewhere.
#   include/search_json.h