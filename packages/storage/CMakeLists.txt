cmake_minimum_required(VERSION 3.21)
project(nothing-storage LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 COMPONENTS Core REQUIRED)

# === Library target ===
add_library(nothing-storage STATIC
    src/IStorageBackend.cpp
    src/StorageContext.cpp
    src/StorageManager.cpp
)

target_include_directories(nothing-storage PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(nothing-storage PUBLIC
    Qt6::Core
)

# Headers for IDE indexing
target_sources(nothing-storage PRIVATE
    include/nothing/storage/StorageModels.h
    include/nothing/storage/IStorageBackend.h
    include/nothing/storage/StorageContext.h
    include/nothing/storage/StorageManager.h
)
