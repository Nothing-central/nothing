cmake_minimum_required(VERSION 3.20)
project(nothing-transport VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── tor ─────────────────────────────────────────────────────────
set(TOR_SOURCES
    tor/src/tor_controller.cpp
)

set(TOR_HEADERS
    tor/include/tor_controller.h
    tor/include/tor_config.h            # header only — no .cpp needed
    tor/include/tor_context_manager.h   # header only — no .cpp needed
)

add_library(nothing-tor STATIC ${TOR_SOURCES})

target_include_directories(nothing-tor PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/tor/include
)

if(WIN32)
    target_link_libraries(nothing-tor PUBLIC ws2_32)
endif()

# ─── proxy ───────────────────────────────────────────────────────
set(PROXY_SOURCES
    proxy/src/proxy_entry.cpp
    proxy/src/proxy_source.cpp
)

set(PROXY_HEADERS
    proxy/include/proxy_entry.h
    proxy/include/proxy_source.h
    proxy/include/proxy_pool.h              # header only — no .cpp needed
    proxy/include/proxy_context_manager.h   # header only — no .cpp needed
)

add_library(nothing-proxy STATIC ${PROXY_SOURCES})

target_include_directories(nothing-proxy PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/proxy/include
)

# ─── Test binaries ───────────────────────────────────────────────
option(TRANSPORT_BUILD_TESTS "Build transport tests" OFF)

if(TRANSPORT_BUILD_TESTS)
    add_executable(test_tor tor/src/test_tor_controller.cpp)
    target_link_libraries(test_tor PRIVATE nothing-tor)

    add_executable(test_proxy proxy/src/test_proxy.cpp)
    target_link_libraries(test_proxy PRIVATE nothing-proxy)
endif()

# ─── Roadmap ─────────────────────────────────────────────────────
# Files to add in future sessions:
#   proxy/src/proxy_http_fetch.cpp — wires ProxySource::FetchFromUrl to
#     Qt's QNetworkAccessManager or piggycpp's HTTP utility.
#   proxy/include/proxy_http_fetch.h
#   tor/src/tor_ipc_handlers.cpp — bridges TorContextManager to piggy's
#     IPC (transport.setTorEnabled, transport.newTorIdentity), same
#     pattern as packages/search/src/search_ipc_handlers.cpp.
#   tor/include/tor_ipc_handlers.h
#   proxy/src/proxy_ipc_handlers.cpp — bridges ProxyContextManager
#     (transport.setProxy, transport.clearProxy, transport.status).
#   proxy/include/proxy_ipc_handlers.h
#   Decide + implement Tor+proxy precedence (see docs.md) wherever the
#     actual QWebEngineProfile network setup happens.