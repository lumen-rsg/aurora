//
// Created by cv2 on 8/14/25.
//

#include "../include/logging.h"
#include "../include/parser.h"
#include <cassert>
#include <vector>

// --- Test Cases ---

void test_valid_package() {
    au::log::info("Running test: Valid Package...");
    auto result = au::Parser::parse("valid_package.yaml");

    assert(result.has_value());
    if (result.has_value()) {
        const auto& pkg = *result;
        assert(pkg.name == "aurora");
        assert(pkg.version == "3.0.1");
        assert(pkg.arch == "x86_64");
        assert(pkg.description == "A test package.");
        assert((pkg.deps == std::vector<std::string>{"sqlite", "yaml-cpp"}));
        assert((pkg.makedepends == std::vector<std::string>{"cmake", "gcc"}));
        assert(pkg.conflicts == std::vector<std::string>{"gradient"});
        assert(pkg.replaces.empty());
        au::log::ok("Test Passed: Valid Package");
    }
}

void test_minimal_package() {
    au::log::info("Running test: Minimal Package...");
    auto result = au::Parser::parse("minimal_package.yaml");

    assert(result.has_value());
    if (result.has_value()) {
        const auto& pkg = *result;
        assert(pkg.name == "tiny-tool");
        assert(pkg.version == "1.0");
        assert(pkg.arch == "any");
        assert(pkg.deps.empty());
        assert(pkg.makedepends.empty());
        au::log::ok("Test Passed: Minimal Package");
    }
}

void test_missing_field() {
    au::log::info("Running test: Missing Required Field...");
    auto result = au::Parser::parse("missing_required.yaml");

    assert(!result.has_value());
    assert(result.error() == au::ParseError::MissingRequiredField);
    au::log::ok("Test Passed: Missing Required Field");
}

void test_invalid_syntax() {
    au::log::info("Running test: Invalid YAML Syntax...");
    auto result = au::Parser::parse("invalid_syntax.yaml");

    assert(!result.has_value());
    assert(result.error() == au::ParseError::InvalidFormat);
    au::log::ok("Test Passed: Invalid YAML Syntax");
}

void test_file_not_found() {
    au::log::info("Running test: File Not Found...");
    auto result = au::Parser::parse("non_existent_file.yaml");

    assert(!result.has_value());
    assert(result.error() == au::ParseError::FileNotFound);
    au::log::ok("Test Passed: File Not Found");
}

int main() {
    try {
        test_valid_package();
        test_minimal_package();
        test_missing_field();
        test_invalid_syntax();
        test_file_not_found();
    } catch (const std::exception& e) {
        au::log::error(std::string("An assertion failed or an unexpected exception occurred: ") + e.what());
        return 1;
    }

    au::log::ok("All parser tests completed successfully!");
    return 0;
}