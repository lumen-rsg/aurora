//
// Created by cv2 on 8/14/25.
//

#include "../include/database.h"
#include "../include/logging.h"
#include <cassert>
#include <filesystem>
#include <algorithm>s

const std::filesystem::path TEST_DB_PATH = "test_au.db";

// Helper to create a sample package
au::InstalledPackage create_sample_pkg(const std::string& name) {
    au::InstalledPackage pkg;
    pkg.name = name;
    pkg.version = "1.0.0";
    pkg.arch = "x86_64";
    pkg.description = "A test package.";
    pkg.deps = {"glibc", "coreutils"};
    pkg.conflicts = {"other-pkg"};
    pkg.install_date = "2023-10-27";
    return pkg;
}

void test_add_and_get() {
    au::log::info("Running test: Add and Get Package...");
    au::Database db(TEST_DB_PATH);
    auto pkg = create_sample_pkg("aurora-test");

    db.add_installed_package(pkg);

    auto retrieved_pkg_opt = db.get_installed_package("aurora-test");
    assert(retrieved_pkg_opt.has_value());

    const auto& retrieved_pkg = *retrieved_pkg_opt;
    assert(retrieved_pkg.name == "aurora-test");
    assert(retrieved_pkg.version == "1.0.0");
    assert((retrieved_pkg.deps == std::vector<std::string>{"glibc", "coreutils"}));
    assert(retrieved_pkg.conflicts == std::vector<std::string>{"other-pkg"});
    assert(retrieved_pkg.replaces.empty());

    au::log::ok("Test Passed: Add and Get Package");
}

void test_is_installed_and_remove() {
    au::log::info("Running test: Is Installed and Remove...");
    std::filesystem::remove(TEST_DB_PATH);
    au::Database db(TEST_DB_PATH);
    auto pkg = create_sample_pkg("temp-pkg");

    assert(!db.is_package_installed("temp-pkg"));

    db.add_installed_package(pkg);
    assert(db.is_package_installed("temp-pkg"));

    db.remove_installed_package("temp-pkg");
    assert(!db.is_package_installed("temp-pkg"));

    au::log::ok("Test Passed: Is Installed and Remove");
}

void test_list_packages() {
    au::log::info("Running test: List Packages...");
    std::filesystem::remove(TEST_DB_PATH);
    au::Database db(TEST_DB_PATH);
    db.add_installed_package(create_sample_pkg("pkg-a"));
    db.add_installed_package(create_sample_pkg("pkg-b"));

    auto installed_list = db.list_installed_packages();
    assert(installed_list.size() == 2);

    // Check if both packages are in the list
    auto has_pkg_a = std::any_of(installed_list.begin(), installed_list.end(),
                                 [](const auto& p){ return p.name == "pkg-a"; });
    auto has_pkg_b = std::any_of(installed_list.begin(), installed_list.end(),
                                 [](const auto& p){ return p.name == "pkg-b"; });
    assert(has_pkg_a && has_pkg_b);

    au::log::ok("Test Passed: List Packages");
}

void test_repo_sync() {
    au::log::info("Running test: Repository Sync...");
    std::filesystem::remove(TEST_DB_PATH);
    au::Database db(TEST_DB_PATH);

    std::vector<au::Package> repo1 = {
            { "coreutils", "9.4", "x86_64", "GNU core utilities", {"glibc"} },
            { "bash", "5.2", "x86_64", "The GNU Bourne-Again SHell", {"glibc", "readline"} }
    };
    db.sync_repo_packages(repo1);

    auto bash_pkg = db.find_repo_package("bash");
    assert(bash_pkg.has_value());
    assert(bash_pkg->version == "5.2");
    assert(!db.find_repo_package("zsh").has_value());

    // Test that a new sync replaces old data
    std::vector<au::Package> repo2 = {
            { "zsh", "5.9", "x86_64", "Z Shell" }
    };
    db.sync_repo_packages(repo2);

    assert(!db.find_repo_package("bash").has_value());
    assert(db.find_repo_package("zsh").has_value());

    au::log::ok("Test Passed: Repository Sync");
}


int main() {
    // Clean up any previous test database file
    std::filesystem::remove(TEST_DB_PATH);

    try {
        test_add_and_get();
        test_is_installed_and_remove();
        test_list_packages();
        test_repo_sync();
    } catch (const std::exception& e) {
        au::log::error(std::string("A database test failed: ") + e.what());
        // Clean up on failure as well
        std::filesystem::remove(TEST_DB_PATH);
        return 1;
    }

    // Clean up on success
    std::filesystem::remove(TEST_DB_PATH);
    au::log::ok("All database tests completed successfully!");
    return 0;
}