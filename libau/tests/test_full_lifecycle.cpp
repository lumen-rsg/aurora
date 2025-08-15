#include "package_manager.h"
#include "logging.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <map>
#include <vector>
#include <sys/wait.h>

// A comprehensive fixture for end-to-end testing.
// Manages a fake system root AND a fake remote repository.
struct LifecycleTestFixture {
    const std::filesystem::path m_sys_root;
    const std::filesystem::path m_repo_root; // The directory for the web server
    const std::filesystem::path m_cache;
    const std::filesystem::path m_db_dir;

    LifecycleTestFixture() :
            m_sys_root("/tmp/aurora_lifecycle_sysroot"),
            m_repo_root("/tmp/aurora_lifecycle_repo"),
            m_cache(m_sys_root / "var" / "cache" / "aurora" / "pkg"),
            m_db_dir(m_sys_root / "var" / "lib" / "aurora")
    {
        std::filesystem::remove_all(m_sys_root);
        std::filesystem::remove_all(m_repo_root);

        std::filesystem::create_directories(m_sys_root / "etc" / "aurora");
        std::filesystem::create_directories(m_cache);
        std::filesystem::create_directories(m_db_dir);

        std::filesystem::create_directories(m_repo_root);

        std::ofstream repos_conf(m_sys_root / "etc" / "aurora" / "repos.conf");
        repos_conf << "core = http://127.0.0.1:8000/core\n";
    }

    ~LifecycleTestFixture() {
        std::filesystem::remove_all(m_sys_root);
        std::filesystem::remove_all(m_repo_root);
    }

    // RAII helper to safely manage changing the current working directory.
    // This is critical for making our interaction with external commands safe.
    class CWDGuard {
    public:
        CWDGuard() : m_old_path(std::filesystem::current_path()) {}
        ~CWDGuard() {
            std::error_code ec;
            std::filesystem::current_path(m_old_path, ec);
        }
    private:
        std::filesystem::path m_old_path;
    };

    // This is the full, correct implementation for creating a package archive.
    void create_mock_package(const std::string& name, const std::string& version,
                             const std::filesystem::path& output_path,
                             const std::vector<std::string>& files)
    {
        const std::string staging_dir_name = name + "-staging";
        const auto staging_dir = std::filesystem::temp_directory_path() / staging_dir_name;
        std::filesystem::remove_all(staging_dir);
        std::filesystem::create_directory(staging_dir);

        // Create metadata file
        std::ofstream meta_file(staging_dir / ".AURORA_META");
        meta_file << "name: \"" << name << "\"\n";
        meta_file << "version: \"" << version << "\"\n";
        meta_file << "arch: \"x86_64\"\n";
        meta_file << "repo_name: \"core\"\n";
        meta_file << "files:\n";
        for(const auto& f : files) meta_file << "  - \"" << f << "\"\n";
        meta_file.close();

        // Create dummy files
        for (const auto& file_path_str : files) {
            const auto full_path = staging_dir / file_path_str;
            if (full_path.has_parent_path()) {
                std::filesystem::create_directories(full_path.parent_path());
            }
            std::ofstream dummy_file(full_path);
            dummy_file << "version " << version;
        }

        CWDGuard guard; // Automatically changes back to original path on scope exit.
        std::filesystem::current_path(staging_dir);

        // 2. The tar command is now trivial and unambiguous. It no longer uses -C.
        std::string command = "tar --zstd -cf " + output_path.string() + " .";

        int status = std::system(command.c_str());
        assert(WEXITSTATUS(status) == 0 && "Failed to create mock package tarball!");
        // When 'guard' is destroyed here, the CWD is restored.

        std::filesystem::remove_all(staging_dir);
    }

    // Function to set up the remote repository state.
    void setup_remote_repo(const std::string& pkg_name, const std::string& version, const std::vector<std::string>& files) {
        const auto repo_pkg_dir = m_repo_root / "core";
        std::filesystem::create_directories(repo_pkg_dir);

        // 1. Create the package archive file in the repo directory.
        const std::string pkg_filename = pkg_name + "-" + version + ".pkg.tar.zst";
        create_mock_package(pkg_name, version, repo_pkg_dir / pkg_filename, files);

        // 2. Create the repo.yaml index file that describes this state.
        std::ofstream repo_index(repo_pkg_dir / "repo.yaml");
        repo_index << "- name: \"" << pkg_name << "\"\n";
        repo_index << "  version: \"" << version << "\"\n";
        repo_index << "  arch: \"x86_64\"\n";
        repo_index << "  repo_name: \"core\"\n";
        repo_index << "  files:\n";
        for(const auto& f : files) {
            repo_index << "    - \"" << f << "\"\n";
        }
        repo_index.close();
    }
};


// --- The End-to-End Test Case ---

void test_full_install_and_update_cycle() {
    au::log::info("Running test: Full Install and Update Cycle");
    LifecycleTestFixture fixture;

    // --- PHASE 1: INSTALLATION ---
    au::log::info("--- INSTALL PHASE ---");
    fixture.setup_remote_repo("test-pkg", "1.0.0", {"usr/bin/prog", "etc/conf.v1"});

    au::PackageManager pm(fixture.m_sys_root);
    assert(pm.sync_database() == true);

    auto install_result = pm.install({"test-pkg"});
    assert(install_result.has_value());

    au::Database db(fixture.m_sys_root / "var/lib/aurora/aurora.db");
    auto installed_v1 = db.get_installed_package("test-pkg");
    assert(installed_v1.has_value() && installed_v1->version == "1.0.0");
    assert(std::filesystem::exists(fixture.m_sys_root / "usr/bin/prog"));
    assert(std::filesystem::exists(fixture.m_sys_root / "etc/conf.v1"));
    au::log::ok("Install phase completed successfully.");

    // --- PHASE 2: UPDATE ---
    au::log::info("--- UPDATE PHASE ---");
    fixture.setup_remote_repo("test-pkg", "2.0.0", {"usr/bin/prog", "etc/conf.v2"});

    auto update_result = pm.update_system();
    assert(update_result.has_value());

    auto installed_v2 = db.get_installed_package("test-pkg");
    assert(installed_v2.has_value() && installed_v2->version == "2.0.0");
    assert(std::filesystem::exists(fixture.m_sys_root / "usr/bin/prog"));
    assert(!std::filesystem::exists(fixture.m_sys_root / "etc/conf.v1"));
    assert(std::filesystem::exists(fixture.m_sys_root / "etc/conf.v2"));

    au::log::ok("Test Passed: Full Install and Update Cycle");
}

int main() {
    au::log::info("--- Full Lifecycle Test Suite ---");
    au::log::info("IMPORTANT: Please run a web server in '/tmp/aurora_lifecycle_repo/'");
    au::log::info("Example: (cd /tmp/aurora_lifecycle_repo && python3 -m http.server 8000)");
    au::log::info("---------------------------------");

    try {
        test_full_install_and_update_cycle();
    } catch (const std::exception& e) {
        au::log::error(std::string("A lifecycle test failed: ") + e.what());
        return 1;
    }

    au::log::ok("All lifecycle tests completed successfully!");
    return 0;
}