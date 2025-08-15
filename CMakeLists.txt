# aurora/CMakeLists.txt

cmake_minimum_required(VERSION 3.22)
project(aurora)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -march=x86-64")
# This enables CTest for the whole project
enable_testing()

# Process the libau directory. libau's CMakeLists.txt will be responsible
# for setting up its own library AND its own tests.
add_subdirectory(libau)
add_subdirectory(au-repotool)