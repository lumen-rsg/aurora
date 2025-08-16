# --- aurora-cli/CMakeLists.txt ---

# Define the executable target.
add_executable(aurora src/main.cpp)

set(CMAKE_CXX_STANDARD 23)
# Link to our library. Because `libau` was added first in the root file,
# CMake knows that "au" is a target within this project.
target_link_libraries(aurora PRIVATE au)