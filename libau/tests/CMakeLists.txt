# --- libau/tests/CMakeLists.txt ---

# All test executables
add_executable(test_parser test_parser.cpp)
add_executable(test_database test_database.cpp)
add_executable(test_resolver test_dependency_resolver.cpp)
add_executable(test_repo_manager test_repository_manager.cpp)
add_executable(test_downloader test_downloader.cpp)
add_executable(test_package_manager test_package_manager.cpp)
add_executable(test_lifecycle test_full_lifecycle.cpp)

# Link each test to the `au` library.
target_link_libraries(test_parser PRIVATE au)
target_link_libraries(test_database PRIVATE au)
target_link_libraries(test_resolver PRIVATE au)
target_link_libraries(test_repo_manager PRIVATE au)
target_link_libraries(test_downloader PRIVATE au)
target_link_libraries(test_package_manager PRIVATE au)
target_link_libraries(test_lifecycle PRIVATE au)

# Set up CTest for each executable.
include(CTest)
add_test(NAME ParserTest COMMAND test_parser)
add_test(NAME DatabaseTest COMMAND test_database)
add_test(NAME ResolverTest COMMAND test_resolver)
add_test(NAME RepoManagerTest COMMAND test_repo_manager)
add_test(NAME DownloaderTest COMMAND test_downloader)
add_test(NAME PackageManagerTest COMMAND test_package_manager)
add_test(NAME LifecycleTest COMMAND test_lifecycle)

# Set the working directory for the parser test.
set_target_properties(test_parser PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)