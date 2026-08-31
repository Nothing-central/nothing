cmake_minimum_required(VERSION 3.20)
project(nothing-lang VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core)

qt_add_library(nothing-lang STATIC
    src/LangManager.cpp
)

target_include_directories(nothing-lang
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(nothing-lang
    PUBLIC Qt6::Core
)

# Copy langs/ folder to wherever the caller's binary ends up
set(NOTHING_LANG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/langs
    CACHE PATH "Path to nothing-lang JSON files" FORCE)