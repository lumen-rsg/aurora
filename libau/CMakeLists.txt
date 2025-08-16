# --- libau/CMakeLists.txt ---

cmake_minimum_required(VERSION 3.22)

# 1. Define the entire project ONE TIME.
project(aurora LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 3. Find all dependencies for all components in ONE place.
find_package(SQLite3 REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(CURL REQUIRED)

# Define the library target.
add_library(au STATIC
        src/archive.cpp
        src/database.cpp
        src/dependency_resolver.cpp
        src/downloader.cpp
        src/package_manager.cpp
        src/parser.cpp
        src/repository.cpp
)

target_include_directories(au PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/vendor"
)

# Link this target to its dependencies.
target_link_libraries(au
        PUBLIC
        SQLite::SQLite3
        PRIVATE
        yaml-cpp::yaml-cpp
        CURL::libcurl
)

# Installation rules remain the same.
install(TARGETS au
        EXPORT au-targets
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
)
install(DIRECTORY include/
        DESTINATION include
)
install(EXPORT au-targets
        FILE libau-targets.cmake
        NAMESPACE au::
        DESTINATION lib/cmake/libau
)

enable_testing()
add_subdirectory(tests)