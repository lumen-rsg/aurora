# --- aurora-cli/CMakeLists.txt ---
cmake_minimum_required(VERSION 3.22)
project(aurora-cli LANGUAGES CXX)
# Define the executable target.

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(yaml-cpp REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(CURL REQUIRED)

add_executable(aurora src/main.cpp)
# Link to our library. Because `libau` was added first in the root file,
# CMake knows that "au" is a target within this project.
target_link_libraries(aurora PRIVATE au yaml-cpp::yaml-cpp SQLite::SQLite3 CURL::libcurl)