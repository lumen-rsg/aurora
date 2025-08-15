# libau/tests/CMakeLists.txt
# Create an executable for our test runner
add_executable(test_parser test_parser.cpp)

# Link the test runner against our main library, libau.
# This is how the test code can access the Parser class.
# au::au is the alias we created in libau/CMakeLists.txt.
# Linking to it also brings in its public dependencies (yaml-cpp, etc.)
target_link_libraries(test_parser PUBLIC au)

# This is important! The test executable needs to find the .yaml files.
# This command sets the working directory for the test runner when executed via CTest.
set_target_properties(test_parser PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

# --- Database Test ---
add_executable(test_database test_database.cpp)
# Our database test also needs to link against the main libau library
target_link_libraries(test_database PRIVATE au)
add_test(NAME DatabaseTest COMMAND test_database)

# --- Dependency Resolver Test ---
add_executable(test_resolver test_dependency_resolver.cpp)
# Our resolver test also needs to link against the main libau library
target_link_libraries(test_resolver PRIVATE au)
add_test(NAME ResolverTest COMMAND test_resolver)

# --- Repository Manager Test ---
add_executable(test_repo_manager test_repository_manager.cpp)
target_link_libraries(test_repo_manager PRIVATE au)
add_test(NAME RepoManagerTest COMMAND test_repo_manager)

# --- Package Manager Test ---
add_executable(test_package_manager test_package_manager.cpp)
target_link_libraries(test_package_manager PRIVATE au)
add_test(NAME PackageManagerTest COMMAND test_package_manager)

add_executable(test_downloader test_downloader.cpp)
target_link_libraries(test_downloader PRIVATE au)
add_test(NAME DownloaderTest COMMAND test_downloader)

# --- Full Lifecycle Test ---
add_executable(test_lifecycle test_full_lifecycle.cpp)
target_link_libraries(test_lifecycle PRIVATE au)
add_test(NAME LifecycleTest COMMAND test_lifecycle)

# Add the test to CTest, CMake's testing framework
include(CTest)
add_test(NAME ParserTest COMMAND test_parser)