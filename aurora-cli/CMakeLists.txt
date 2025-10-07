# NO project() or find_package() calls!

add_executable(aurora src/main.cpp) # Assuming ui_helpers has a .cpp

# Link to our library. Because `au` was defined in the parent scope,
# CMake knows it's a target within this project.
# Linking to `au` will automatically pull in all of its PUBLIC include directories
# and link dependencies (yaml-cpp, curl, sqlite3, etc.).
target_link_libraries(aurora PRIVATE au)

# If a static build was requested, add the linker flag to this executable.
if(BUILD_AURORA_STATIC)
    # This is the "big hammer" that tells the linker to not use any shared libraries
    # for the final binary, including libc.
    target_link_options(aurora PRIVATE -static)
endif()

# Simple install rule for the final binary
install(TARGETS aurora DESTINATION bin)