# Define the library target.
add_library(au STATIC
        src/archive.cpp
        src/database.cpp
        src/dependency_resolver.cpp
        src/downloader.cpp
        src/package_manager.cpp
        src/parser.cpp
        src/repository.cpp
        src/crypto.cpp
)

# --- CORRECTED AND CONSOLIDATED USAGE REQUIREMENTS ---

# This single block defines all usage requirements for the 'au' target.
target_include_directories(au
        # PRIVATE: The implementation files of 'au' need access to their own headers.
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"

        # PUBLIC: System dependencies are needed by both 'au' and its consumers.
        # These are absolute system paths, which is fine.
        PUBLIC
        ${GPGME_INCLUDE_DIRS}

        # INTERFACE: This is what consumers of 'au' (like aurora-cli) need.
        INTERFACE
        # For consumers inside this project build, give them the absolute source path.
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>

        # For consumers after this library is installed, tell them the headers
        # will be in a relative 'include' directory.
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(au
        # PUBLIC: All of these libraries are required by the implementation of 'au'
        # and transitively by any executable that links to 'au'.
        PUBLIC
        SQLite::SQLite3
        yaml-cpp::yaml-cpp
        CURL::libcurl
        LibArchive::LibArchive
        ${GPGME_LIBRARIES}
        acl
        lua
)

# --- Installation rules remain the same ---
install(TARGETS au
        EXPORT au-targets
        ARCHIVE DESTINATION lib
)
install(DIRECTORY include/
        DESTINATION include
)
install(EXPORT au-targets
        FILE libau-targets.cmake
        NAMESPACE au::
        DESTINATION lib/cmake/libau
)