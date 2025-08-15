//
// Created by cv2 on 8/14/25.
//

#include "database.h"
#include "dependency_resolver.h"
#include "logging.h"
#include <cassert>
#include <filesystem>
#include <vector>

const std::filesystem::path TEST_DB_PATH = "resolver_test.db";

// Helper to check if the resolved list is correct
void assert_order(const au::ResolutionList& list, const std::vector<std::string>& expected) {
    assert(list.size() == expected.size());
    for (size_t i = 0; i < list.size(); ++i) {
        assert(list[i].name == expected[i]);
    }
}

void test_linear_chain() {
    au::log::info("Running test: Linear Dependency Chain (C -> B -> A)");
    std::filesystem::remove(TEST_DB_PATH);
    au::Database db(TEST_DB_PATH);
    au::DependencyResolver resolver(db);

    // Setup: C depends on B, B depends on A
    db.sync_repo_packages({
                                  { "A", "1.0", "any", "Base package" },
                                  { "B", "1.0", "any", "Mid package", {"A"} },
                                  { "C", "1.0", "any", "Top package", {"B"} }
                          });

    auto result = resolver.resolve({"C"});
    assert(result.has_value());
    assert_order(*result, {"A", "B", "C"});

    au::log::ok("Test Passed: Linear Chain");
}

void test_diamond_dependency() {
    au::log::info("Running test: Diamond Dependency (D -> B, D -> C, B -> A, C -> A)");
    std::filesystem::remove(TEST_DB_PATH);
    au::Database db(TEST_DB_PATH);
    au::DependencyResolver resolver(db);

    db.sync_repo_packages({
                                  { "A", "1.0", "any" },
                                  { "B", "1.0", "any", "", {"A"} },
                                  { "C", "1.0", "any", "", {"A"} },
                                  { "D", "1.0", "any", "", {"B", "C"} }
                          });

    auto result = resolver.resolve({"D"});
    assert(result.has_value());
    const auto& list = *result;
    assert(list.size() == 4);
    assert(list[0].name == "A"); // A must be first
    assert(list[3].name == "D"); // D must be last
    // B and C can be in either order
    assert((list[1].name == "B" && list[2].name == "C") || (list[1].name == "C" && list[2].name == "B"));

    au::log::ok("Test Passed: Diamond Dependency");
}

void test_already_installed() {
    au::log::info("Running test: Dependency Already Installed");
    std::filesystem::remove(TEST_DB_PATH);
    au::Database db(TEST_DB_PATH);
    au::DependencyResolver resolver(db);

    // Setup: A is installed, B depends on A
    au::InstalledPackage installed_A;
    installed_A.name = "A";
    db.add_installed_package(installed_A);
    db.sync_repo_packages({
                                  { "A", "1.0", "any" },
                                  { "B", "1.0", "any", "", {"A"} }
                          });

    auto result = resolver.resolve({"B"});
    assert(result.has_value());
    assert_order(*result, {"B"}); // Should only resolve to install B

    au::log::ok("Test Passed: Already Installed");
}

void test_circular_dependency() {
    au::log::info("Running test: Circular Dependency (A -> B -> A)");
    std::filesystem::remove(TEST_DB_PATH);
    au::Database db(TEST_DB_PATH);
    au::DependencyResolver resolver(db);

    db.sync_repo_packages({
                                  { "A", "1.0", "any", "", {"B"} },
                                  { "B", "1.0", "any", "", {"A"} }
                          });

    auto result = resolver.resolve({"A"});
    assert(!result.has_value());
    assert(result.error() == au::ResolveError::CircularDependency);

    au::log::ok("Test Passed: Circular Dependency");
}

void test_dependency_not_found() {
    au::log::info("Running test: Dependency Not Found (A -> B, B is missing)");
    std::filesystem::remove(TEST_DB_PATH);
    au::Database db(TEST_DB_PATH);
    au::DependencyResolver resolver(db);

    db.sync_repo_packages({
                                  { "A", "1.0", "any", "", {"B"} }
                                  // B is not in the repo
                          });

    auto result = resolver.resolve({"A"});
    assert(!result.has_value());
    assert(result.error() == au::ResolveError::DependencyNotFound);

    au::log::ok("Test Passed: Dependency Not Found");
}


int main() {
    try {
        test_linear_chain();
        test_diamond_dependency();
        test_already_installed();
        test_circular_dependency();
        test_dependency_not_found();
    } catch (const std::exception& e) {
        au::log::error(std::string("A resolver test failed: ") + e.what());
        std::filesystem::remove(TEST_DB_PATH);
        return 1;
    }

    std::filesystem::remove(TEST_DB_PATH);
    au::log::ok("All dependency resolver tests completed successfully!");
    return 0;
}