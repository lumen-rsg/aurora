# aurora/CMakeLists.txt

# --- Root CMakeLists.txt ---
cmake_minimum_required(VERSION 3.22)

# 1. Define the entire project ONE TIME.
project(aurora LANGUAGES CXX)

# 2. Set the C++ standard for the entire project. This is forceful and unambiguous.
#    Every target added from here on will inherit this setting.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 3. Find all dependencies for all components in ONE place.
find_package(SQLite3 REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(CURL REQUIRED)

# 4. Add all of our components. The order is important.
#    `libau` must be defined before the executables that link to it.
add_subdirectory(libau)
add_subdirectory(aurora-cli)
add_subdirectory(au-repotool)

# 5. Add the testing suite at the end.
enable_testing()
add_subdirectory(libau/tests)