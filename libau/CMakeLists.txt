# libau/CMakeLists.txt

cmake_minimum_required(VERSION 3.22)
project(libau LANGUAGES CXX)

# --- Configuration ---
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Library Definition ---
add_library(au STATIC
        src/parser.cpp
        include/sqlite_orm.h
        src/database.cpp
        src/database.cpp
        src/dependency_resolver.cpp
        include/downloader.h
        src/downloader.cpp
        src/package_manager.cpp
        src/repository.cpp
        include/archive.h
        src/archive.cpp
)

# --- Dependencies ---
find_package(SQLite3 REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(CURL REQUIRED)
find_package(LibArchive REQUIRED)

target_link_libraries(au
        PUBLIC
        SQLite::SQLite3
        PRIVATE
        yaml-cpp::yaml-cpp
        CURL::libcurl
        LibArchive::LibArchive
)

target_include_directories(au
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>      # when “make install” is used
)

# --- Installation ---
install(TARGETS au
        EXPORT au-targets
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
)
install(DIRECTORY include/ DESTINATION include)
install(EXPORT au-targets
        FILE libau-targets.cmake
        NAMESPACE au::
        DESTINATION lib/cmake/libau
)

# --- Add the tests for this library ---
add_subdirectory(tests)