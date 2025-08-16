# au-repotool/CMakeLists.txt

cmake_minimum_required(VERSION 3.22)
project(repotool LANGUAGES CXX)

# 2. Set the C++ standard for the entire project. This is forceful and unambiguous.
#    Every target added from here on will inherit this setting.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(yaml-cpp REQUIRED)
find_package(au REQUIRED)

# 3. Find all dependencies for all components in ONE place.

add_executable(repotool src/main.cpp)
target_include_directories(repotool PRIVATE "/usr/local/include")

target_link_libraries(repotool PRIVATE au yaml-cpp::yaml-cpp)