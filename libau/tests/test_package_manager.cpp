//
// Created by cv2 on 8/15/25.
//

#include "../include/package_manager.h"
#include "../include/logging.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <map>
#include <vector>
#include <sys/wait.h>

class CWDGuard {
public:
    CWDGuard() : m_old_path(std::filesystem::current_path()) {}
    ~CWDGuard() { std::error_code ec; std::filesystem::current_path(m_old_path, ec); }
private:
    std::filesystem::path m_old_path;
};

struct PackageManagerTestFixture {
    const std::filesystem::path m_sys_root;
    const std::filesystem::path m_repo_root;
    const std::filesystem::path m_cache;

    PackageManagerTestFixture() :
            m_sys_root("/tmp/aurora_pm_test_root"),
            m_repo_root("/tmp/aurora_pm_test_repo"),
            m_cache(m_sys_root / "var" / "cache" / "aurora" / "pkg")
    {
        std::filesystem::remove_all(m_sys_root);
        std::filesystem::remove_all(m_repo_root);

        std::filesystem::create_directories(m_sys_root / "etc" / "aurora");
        std::filesystem::create_directories(m_cache);
        std::filesystem::create_directories(m_sys_root / "var" / "lib" / "aurora");

        std::filesystem::create_directories(m_repo_root / "core");

        std::ofstream repos_conf(m_sys_root / "etc" / "aurora" / "repos.conf");
        repos_conf << "core = http://127.0.0.1:8000/core\n";
    }

    ~PackageManagerTestFixture() {
        std::filesystem::remove_all(m_sys_root);
        std::filesystem::remove_all(m_repo_root);
    }

    void create_mock_package(const std::string& name, const std::string& version,
                             const std::filesystem::path& output_path,
                             const std::vector<std::string>& deps = {},
                             const std::vector<std::string>& files_arg = {},
                             const std::vector<std::string>& conflicts = {},
                             const std::map<std::string, std::string>& scripts = {}) {
        const auto staging_dir = std::filesystem::temp_directory_path() / (name + "-staging");
        std::filesystem::remove_all(staging_dir);
        std::filesystem::create_directory(staging_dir);
        auto all_files = files_arg;
        for (const auto& [type, content] : scripts) {
            std::filesystem::path script_path = std::filesystem::path("scripts") / (type + ".sh");
            all_files.push_back(script_path.string());
            std::filesystem::create_directory(staging_dir / "scripts");
            std::ofstream script_file(staging_dir / script_path);
            script_file << "#!/bin/sh\n" << content << "\n";
        }
        std::ofstream meta_file(staging_dir / ".AURORA_META");
        meta_file << "name: \"" << name << "\"\n" << "version: \"" << version << "\"\n" << "arch: \"x86_64\"\n" << "repo_name: \"core\"\n";
        if (!deps.empty()) { meta_file << "deps:\n"; for(const auto& d : deps) meta_file << "  - " << d << "\n"; }
        if (!all_files.empty()) { meta_file << "files:\n"; for(const auto& f : all_files) meta_file << "  - \"" << f << "\"\n"; }
        if (!conflicts.empty()) { meta_file << "conflicts:\n"; for(const auto& c : conflicts) meta_file << "  - " << c << "\n"; }
        for (const auto& [type, content] : scripts) { meta_file << type << ": \"scripts/" << type << ".sh\"\n"; }
        meta_file.close();
        for (const auto& file_path_str : files_arg) {
            const auto full_path = staging_dir / file_path_str;
            if (full_path.has_parent_path()) std::filesystem::create_directories(full_path.parent_path());
            std::ofstream dummy_file(full_path); dummy_file << "content";
        }
        CWDGuard guard;
        std::filesystem::current_path(staging_dir);
        std::string command = "tar --zstd -cf " + output_path.string() + " .";
        int status = std::system(command.c_str());
        assert(WEXITSTATUS(status) == 0);
        std::filesystem::remove_all(staging_dir);
    }

    void setup_remote_repo(const std::vector<au::Package>& packages) {
        std::ofstream repo_index(m_repo_root / "core" / "repo.yaml");
        for (const auto& pkg : packages) {
            // FIX #2: Convert vector<path> to vector<string> before calling helper
            std::vector<std::string> file_strings;
            for (const auto& p : pkg.files) {
                file_strings.push_back(p.string());
            }
            create_mock_package(pkg.name, pkg.version, m_repo_root / "core" / (pkg.name + "-" + pkg.version + ".pkg.tar.zst"), pkg.deps, file_strings);

            repo_index << "- name: \"" << pkg.name << "\"\n";
            repo_index << "  version: \"" << pkg.version << "\"\n";
            repo_index << "  arch: \"x86_64\"\n";
            repo_index << "  repo_name: \"core\"\n";
            repo_index << "  pre_install_script: \"" << pkg.pre_install_script << "\"\n";
            if (!pkg.deps.empty()) { repo_index << "  deps:\n"; for(const auto& d : pkg.deps) repo_index << "    - " << d << "\n"; }
            if (!pkg.files.empty()) { repo_index << "  files:\n"; for(const auto& f : pkg.files) repo_index << "    - \"" << f.string() << "\"\n"; }
        }
    }
};

// --- Test Cases ---

void test_local_install_success() {
    au::log::info("Running test: Local Install Success");
    PackageManagerTestFixture fixture;
    au::PackageManager pm(fixture.m_sys_root);
    const auto pkg_path = fixture.m_cache / "pkg-a-1.0.au";
    fixture.create_mock_package("pkg-a", "1.0", pkg_path, {}, {"usr/bin/a-prog"});
    auto result = pm.install_local_package(pkg_path);
    assert(result.has_value());
    assert(std::filesystem::exists(fixture.m_sys_root / "usr/bin/a-prog"));
    au::Database db(fixture.m_sys_root / "var/lib/aurora/aurora.db");
    assert(db.is_package_installed("pkg-a"));
    au::log::ok("Test Passed: Local Install Success");
}

void test_local_install_dep_fail() {
    au::log::info("Running test: Local Install Dependency Fail");
    PackageManagerTestFixture fixture;
    au::PackageManager pm(fixture.m_sys_root);
    const auto pkg_path = fixture.m_cache / "pkg-b-1.0.au";
    fixture.create_mock_package("pkg-b", "1.0", pkg_path, {"dep-a"});
    auto result = pm.install_local_package(pkg_path);
    assert(!result.has_value());
    au::Database db(fixture.m_sys_root / "var/lib/aurora/aurora.db");
    assert(!db.is_package_installed("pkg-b"));
    au::log::ok("Test Passed: Local Install Dependency Fail");
}

void test_transaction_install_success() {
    au::log::info("Running test: Transaction Install Success (B->A)");
    PackageManagerTestFixture fixture;
    au::PackageManager pm(fixture.m_sys_root);
    au::Package pkg_a, pkg_b;
    pkg_a.name = "pkg-a"; pkg_a.version = "1.0"; pkg_a.files = {"usr/bin/a-prog"};
    pkg_b.name = "pkg-b"; pkg_b.version = "1.0"; pkg_b.deps = {"pkg-a"}; pkg_b.files = {"usr/bin/b-prog"};
    fixture.setup_remote_repo({pkg_a, pkg_b});
    assert(pm.sync_database());
    auto result = pm.install({"pkg-b"});
    assert(result.has_value());
    assert(std::filesystem::exists(fixture.m_sys_root / "usr/bin/a-prog"));
    assert(std::filesystem::exists(fixture.m_sys_root / "usr/bin/b-prog"));
    au::Database db(fixture.m_sys_root / "var/lib/aurora/aurora.db");
    assert(db.is_package_installed("pkg-a"));
    assert(db.is_package_installed("pkg-b"));
    au::log::ok("Test Passed: Transaction Install Success");
}

void test_transaction_rollback() {
    au::log::info("Running test: Transaction Rollback on Script Failure");
    PackageManagerTestFixture fixture;
    au::PackageManager pm(fixture.m_sys_root);
    au::Package pkg_a, pkg_b;
    pkg_a.name = "pkg-a"; pkg_a.version = "1.0"; pkg_a.files = {"usr/bin/a-prog"};
    pkg_b.name = "pkg-b"; pkg_b.version = "1.0"; pkg_b.deps = {"pkg-a"}; pkg_b.files = {"usr/bin/b-prog"};
    pkg_b.pre_install_script = "scripts/pre_install.sh";
    fixture.setup_remote_repo({pkg_a, pkg_b});
    fixture.create_mock_package("pkg-b", "1.0", fixture.m_repo_root / "core" / "pkg-b-1.0.pkg.tar.zst", {"pkg-a"}, {"usr/bin/b-prog"}, {}, {{ "pre_install", "exit 1" }});
    assert(pm.sync_database());
    auto result = pm.install({"pkg-b"});
    assert(!result.has_value());
    assert(result.error() == au::TransactionError::ScriptletFailed);
    // FIX #3: Declare db object for verification
    au::Database db(fixture.m_sys_root / "var/lib/aurora/aurora.db");
    assert(!db.is_package_installed("pkg-a"));
    assert(!std::filesystem::exists(fixture.m_sys_root / "usr/bin/a-prog"));
    au::log::ok("Test Passed: Transaction Rollback");
}


void test_remove_success() {
    au::log::info("Running test: Remove Success");
    PackageManagerTestFixture fixture;
    au::PackageManager pm(fixture.m_sys_root);

    // Setup: Install a package first
    const auto pkg_path = fixture.m_cache / "pkg-a-1.0.au";
    fixture.create_mock_package("pkg-a", "1.0", pkg_path, {}, {"usr/bin/a-prog"});
    auto install_result = pm.install_local_package(pkg_path);
    assert(install_result.has_value());
    assert(std::filesystem::exists(fixture.m_sys_root / "usr/bin/a-prog"));

    // Action: Remove the package
    auto remove_result = pm.remove({"pkg-a"});
    assert(remove_result.has_value());

    // Verification
    au::Database db(fixture.m_sys_root / "var/lib/aurora/aurora.db");
    assert(!db.is_package_installed("pkg-a"));
    assert(!std::filesystem::exists(fixture.m_sys_root / "usr/bin/a-prog"));
    // Check that the now-empty parent directory was also removed
    assert(!std::filesystem::exists(fixture.m_sys_root / "usr/bin"));

    au::log::ok("Test Passed: Remove Success");
}

void test_remove_dependency_violation() {
    au::log::info("Running test: Remove Dependency Violation");
    PackageManagerTestFixture fixture;
    au::PackageManager pm(fixture.m_sys_root);

    // Setup: Install two packages, B depends on A
    fixture.create_mock_package("pkg-a", "1.0", fixture.m_cache / "pkg-a-1.0.pkg.tar.zst", {}, {"usr/bin/a-prog"});
    fixture.create_mock_package("pkg-b", "1.0", fixture.m_cache / "pkg-b-1.0.pkg.tar.zst", {"pkg-a"}, {"usr/bin/b-prog"});
    au::Database db(fixture.m_sys_root / "var/lib/aurora/aurora.db");
    au::Package pkg_a, pkg_b;
    pkg_a.name = "pkg-a"; pkg_a.version = "1.0";
    pkg_b.name = "pkg-b"; pkg_b.version = "1.0"; pkg_b.deps = {"pkg-a"};
    db.sync_repo_packages({pkg_a, pkg_b});
    auto install_result = pm.install({"pkg-b"});
    assert(install_result.has_value());

    // Action: Attempt to remove A, which B depends on. This MUST fail.
    auto remove_result = pm.remove({"pkg-a"});
    assert(!remove_result.has_value());
    assert(remove_result.error() == au::TransactionError::DependencyViolation);

    // Verification: Ensure the system state is unchanged
    assert(db.is_package_installed("pkg-a"));
    assert(db.is_package_installed("pkg-b"));
    assert(std::filesystem::exists(fixture.m_sys_root / "usr/bin/a-prog"));
    assert(std::filesystem::exists(fixture.m_sys_root / "usr/bin/b-prog"));

    au::log::ok("Test Passed: Remove Dependency Violation");
}

void test_remove_with_scripts() {
    au::log::info("Running test: Remove with Scripts");
    PackageManagerTestFixture fixture;
    au::PackageManager pm(fixture.m_sys_root);
    const auto marker_file_path = fixture.m_sys_root / "tmp/pre_remove_was_run";

    // Setup: Install a package with a pre-remove script
    const auto pkg_path = fixture.m_cache / "pkg-script-1.0.au";

    // The script must use the first argument ($1) as its root directory.
    // The quotes around "$1" are important to handle paths with spaces.
    const std::string script_content = "mkdir -p \"$1/tmp\" && echo 'hello' > \"$1/tmp/pre_remove_was_run\"";

    fixture.create_mock_package("pkg-script", "1.0", pkg_path, {}, {"usr/bin/script-prog"}, {},
                                {{ "pre_remove", script_content }});

    auto install_result = pm.install_local_package(pkg_path);
    assert(install_result.has_value());

    // Action: Remove the package
    auto remove_result = pm.remove({"pkg-script"});
    assert(remove_result.has_value());

    // Verification
    assert(!std::filesystem::exists(fixture.m_sys_root/ "usr/bin/script-prog"));
    // This assertion will now pass because the script respects the $1 argument.
    assert(std::filesystem::exists(marker_file_path));

    au::log::ok("Test Passed: Remove with Scripts");
}


int main() {
    try {
        test_local_install_success();
        test_local_install_dep_fail();
        test_transaction_install_success();
        test_transaction_rollback();
        test_remove_success();
        test_remove_dependency_violation();
        test_remove_with_scripts();
    } catch (const std::exception& e) {
        au::log::error(std::string("A package manager test failed: ") + e.what());
        return 1;
    }

    au::log::ok("All package manager tests completed successfully!");
    return 0;
}