# NO project() or find_package() calls!

add_executable(repotool src/main.cpp)

# Just like the main CLI, link to `au` and let CMake handle the dependencies.
target_link_libraries(repotool PRIVATE au)

if(BUILD_AURORA_STATIC)
    target_link_options(repotool PRIVATE -static)
endif()

install(TARGETS repotool DESTINATION bin)