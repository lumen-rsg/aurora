//
// Created by cv2 on 8/15/25.
//

#include "database.h"
#include "repository.h"
#include "logging.h"
#include <cassert>
#include <filesystem>

// Define paths relative to the project's root directory
const std::filesystem::path TEST_DB_PATH = "repo_manager_test.db";
// NOTE: The executable runs from the `build` directory, so we need to go up
// and over to the source directory where the test files are.
const std::filesystem::path REPO_CONFIG_PATH = "../../../libau/tests/test_repo_setup/repos.conf";


void test_repository_update() {
    au::log::info("Running test: Full Repository Update");

    // --- Setup ---
    std::filesystem::remove(TEST_DB_PATH);
    au::Database db(TEST_DB_PATH);

    // Check that the config file actually exists before we start.
    assert(std::filesystem::exists(REPO_CONFIG_PATH));
    au::RepositoryManager repo_manager(db, REPO_CONFIG_PATH);

    // --- Action ---
    bool success = repo_manager.update_all();
    assert(success == true); // The overall operation should succeed

    // --- Verification ---
    au::log::info("Verifying database contents after sync...");

    // Check packages from the 'core' repository
    auto glibc_pkg = db.find_repo_package("glibc");
    assert(glibc_pkg.has_value());
    assert(glibc_pkg->version == "2.38");

    auto coreutils_pkg = db.find_repo_package("coreutils");
    assert(coreutils_pkg.has_value());
    assert(coreutils_pkg->deps.size() == 1 && coreutils_pkg->deps[0] == "glibc");

    auto bash_pkg = db.find_repo_package("bash");
    assert(bash_pkg.has_value());

    // Check packages from the 'extra' repository
    auto nano_pkg = db.find_repo_package("nano");
    assert(nano_pkg.has_value());
    assert(nano_pkg->version == "7.2");

    auto curl_pkg = db.find_repo_package("curl");
    assert(curl_pkg.has_value());

    // Crucially, check that the malformed package was skipped and NOT added
    auto broken_pkg = db.find_repo_package("broken-package");
    assert(!broken_pkg.has_value());

    // Check for a package that doesn't exist at all
    auto fake_pkg = db.find_repo_package("non-existent-package");
    assert(!fake_pkg.has_value());

    au::log::ok("Test Passed: Full Repository Update");
}

int main() {
    au::log::info("Make sure the local Python web server is running in 'libau/tests/test_repo_setup/'");

    try {
        test_repository_update();
    } catch (const std::exception& e) {
        au::log::error(std::string("A repository manager test failed: ") + e.what());
        std::filesystem::remove(TEST_DB_PATH);
        return 1;
    }

    std::filesystem::remove(TEST_DB_PATH);
    au::log::ok("All repository manager tests completed successfully!");
    return 0;
}