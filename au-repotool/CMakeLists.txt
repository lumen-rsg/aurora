# au-repotool/CMakeLists.txt

cmake_minimum_required(VERSION 3.22)
project(repotool LANGUAGES CXX)

# Create the executable
add_executable(repotool src/main.cpp)

# Link it against our core library, libau.
# au is the alias we created in libau/CMakeLists.txt.
# This gives us access to the parser, logging, archive functions, etc.
target_link_libraries(repotool PRIVATE au)