cmake_minimum_required(VERSION 3.22)

# 1. Define the entire project ONE TIME.
project(aurora LANGUAGES CXX)

# 2. Set global settings for all sub-projects.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 3. Add an option for the user to request fully static executables.
option(BUILD_AURORA_STATIC "Build fully static aurora executables" OFF)
if(BUILD_AURORA_STATIC)
    message(STATUS "Static build requested. This requires static versions of all dependencies (glibc-static, etc.).")
    # Tell find_package to prefer static libraries (.a) over shared ones (.so)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
    # For some packages, we need to explicitly ask for static libs
    set(CURL_USE_STATIC_LIBS TRUE)
endif()

# 4. Find all dependencies for all components in ONE place.
find_package(SQLite3 REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(CURL REQUIRED)
# GPGME has a typo in your original file, it needs libarchive too.
find_package(LibArchive REQUIRED)
pkg_check_modules(GPGME REQUIRED gpgme)
find_package(Lua REQUIRED)

# 5. Add the subdirectories. CMake will now treat them as part of this single, larger project.
add_subdirectory(libau)
add_subdirectory(aurora-cli)
add_subdirectory(aurora-repotool)