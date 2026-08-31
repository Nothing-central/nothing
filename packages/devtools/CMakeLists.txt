cmake_minimum_required(VERSION 3.21)
project(nothing-devtools LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 COMPONENTS Core WebSockets Network WebEngineCore REQUIRED)

# === Library target ===
add_library(nothing-devtools STATIC
    src/CdpClient.cpp
    src/DevToolsServer.cpp
    src/NetworkTracker.cpp
    src/RuntimeTracker.cpp
    src/DomTracker.cpp
    src/PageTracker.cpp
    src/StorageTracker.cpp
    src/TargetManager.cpp
)

target_include_directories(nothing-devtools PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(nothing-devtools PUBLIC
    Qt6::Core
    Qt6::WebSockets
    Qt6::Network
    Qt6::WebEngineCore
)

# Headers for IDE indexing
target_sources(nothing-devtools PRIVATE
    include/nothing/devtools/DevToolsModels.h
    include/nothing/devtools/CdpClient.h
    include/nothing/devtools/DevToolsServer.h
    include/nothing/devtools/NetworkTracker.h
    include/nothing/devtools/RuntimeTracker.h
    include/nothing/devtools/DomTracker.h
    include/nothing/devtools/PageTracker.h
    include/nothing/devtools/StorageTracker.h
    include/nothing/devtools/TargetManager.h
)
