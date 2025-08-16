# --- libau/CMakeLists.txt ---

# Define the library target.
add_library(au STATIC
        src/archive.cpp
        src/database.cpp
        src/dependency_resolver.cpp
        src/downloader.cpp
        src/package_manager.cpp
        src/parser.cpp
        src/repository.cpp
)

# Define the include directories for this target.
# PUBLIC: Anyone who links to `au` will automatically get this include path.
target_include_directories(au
        PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        PRIVATE
        "${CMAKE_SOURCE_DIR}/vendor"
)

# Link this target to its dependencies.
target_link_libraries(au
        PUBLIC
        SQLite::SQLite3
        PRIVATE
        yaml-cpp::yaml-cpp
        CURL::libcurl
)

# Installation rules remain the same.
install(TARGETS au
        EXPORT au-targets
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
)
install(DIRECTORY include/
        DESTINATION include
)
install(EXPORT au-targets
        FILE libau-targets.cmake
        NAMESPACE au::
        DESTINATION lib/cmake/libau
)