//
// Created by cv2 on 8/14/25.
//

#include "libau/package_manager.h"
#include "libau/downloader.h"
#include "libau/logging.h"
#include "libau/archive.h" // Needed for extraction
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include "libau/parser.h"
#include <map>
#include <ranges>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <sys/acl.h>
#include <unistd.h> // for readlink, symlink, chown

#include "libau/crypto.h"

namespace au {

    static bool replicate_object(const std::filesystem::path& from, const std::filesystem::path& to) {
    // 1. Get metadata of the source object WITHOUT following symlinks.
    struct stat statbuf{};
    if (lstat(from.c_str(), &statbuf) != 0) {
        log::error("Failed to lstat source object: " + from.string());
        return false;
    }

    // 2. Replicate the object based on its type.
    switch (statbuf.st_mode & S_IFMT) {
        case S_IFLNK: { // It's a symbolic link
            std::vector<char> buf(statbuf.st_size + 1);
            ssize_t len = readlink(from.c_str(), buf.data(), buf.size());
            if (len == -1) {
                log::error("Failed to read symlink: " + from.string());
                return false;
            }
            buf[len] = '\0';
            if (symlink(buf.data(), to.c_str()) != 0) {
                log::error("Failed to create symlink: " + to.string());
                return false;
            }
            break;
        }

        case S_IFREG: { // It's a regular file
            std::ifstream in(from, std::ios::binary);
            if (!in) { log::error("Failed to open source file: " + from.string()); return false; }
            std::ofstream out(to, std::ios::binary | std::ios::trunc);
            if (!out) { log::error("Failed to create destination file: " + to.string()); return false; }
            out << in.rdbuf();
            break;
        }

        case S_IFCHR: // Character device
        case S_IFBLK: { // Block device
            if (mknod(to.c_str(), statbuf.st_mode, statbuf.st_rdev) != 0) {
                log::error("Failed to create device node: " + to.string());
                return false;
            }
            break;
        }

        case S_IFDIR: { // Directory
             if (mkdir(to.c_str(), statbuf.st_mode) != 0) {
                 log::error("Failed to create directory: " + to.string());
                 return false;
             }
             break;
        }

        default:
            log::error("Unsupported file type for replication: " + from.string());
            return false;
    }

    // 3. Copy metadata (permissions, ownership, xattrs, ACLs) for the new object.
    // For symlinks, we use lchown/lchmod (though they are often ignored).
    // For everything else, we use the standard calls.

    // Ownership
    if ((S_IFLNK == (statbuf.st_mode & S_IFMT)) ?
         lchown(to.c_str(), statbuf.st_uid, statbuf.st_gid) != 0 :
         chown(to.c_str(), statbuf.st_uid, statbuf.st_gid) != 0) {
        log::error("Failed to chown destination object: " + to.string());
    }

    // Permissions (ignored for symlinks)
    if (S_IFLNK != (statbuf.st_mode & S_IFMT)) {
        if (chmod(to.c_str(), statbuf.st_mode) != 0) {
            log::error("Failed to chmod destination object: " + to.string());
        }
    }

    // Extended Attributes (using lsetxattr for symlinks)
    ssize_t list_size = llistxattr(from.c_str(), nullptr, 0);
    if (list_size > 0) {
        std::vector<char> names(list_size);
        llistxattr(from.c_str(), names.data(), names.size());
        for (const char* name = names.data(); name < names.data() + list_size; name += strlen(name) + 1) {
            ssize_t value_size = lgetxattr(from.c_str(), name, nullptr, 0);
            if (value_size >= 0) {
                std::vector<char> value(value_size);
                lgetxattr(from.c_str(), name, value.data(), value.size());
                lsetxattr(to.c_str(), name, value.data(), value.size(), 0);
            }
        }
    }

    // ACLs
    acl_t acl = acl_get_file(from.c_str(), ACL_TYPE_ACCESS);
    if (acl) {
        if (acl_set_file(to.c_str(), ACL_TYPE_ACCESS, acl) != 0) {
            log::error("Failed to set ACL on destination object: " + to.string());
        }
        acl_free(acl);
    }

    return true;
}

    static void log_space_error(const std::string& path, int64_t required, int64_t available) {
        auto to_mb = [](int64_t bytes) { return bytes / (1024 * 1024); };
        log::error("Not enough free space on " + path);
        log::error("  Required: " + std::to_string(to_mb(required)) + " MB");
        log::error("  Available: " + std::to_string(to_mb(available)) + " MB");
    }

    // Implement the previously stubbed method
    std::expected<void, TransactionError> PackageManager::prepare_transaction_assets(Transaction& transaction) {
        if (transaction.to_install.empty()) {
            return {};
        }

        log::info("Downloading transaction assets...");
        Downloader downloader;

        // The `jobs` vector can become very large. To prevent a stack overflow during
        // the deep calls into libcurl, we allocate the vector itself on the heap.
        auto jobs = std::make_unique<std::vector<DownloadJob>>();
        jobs->reserve(transaction.to_install.size());

        std::vector<std::filesystem::path> download_paths;
        download_paths.reserve(transaction.to_install.size());

        for (const auto& item : transaction.to_install) {
            const auto& pkg = item.metadata;

            // NEW: Get the list of mirrors
            auto repo_urls_opt = m_repo_manager.get_repo_urls(pkg.repo_name);
            if (!repo_urls_opt) {
                log::error("Cannot find repository URL for repo '" + pkg.repo_name + "'.");
                return std::unexpected(TransactionError::ResolutionFailed);
            }

            // Construct a list of full download URLs from the mirrors
            std::vector<std::string> download_urls;
            for (const auto& base_url : *repo_urls_opt) {
                download_urls.push_back(base_url + "/" + pkg.name + "-" + pkg.version + ".au");
            }

            download_paths.push_back(m_cache_path / (pkg.name + "-" + pkg.version + ".au"));
            jobs->emplace_back(
                    std::move(download_urls), // Pass the whole list
                    download_paths.back(),
                    pkg.name + "-" + pkg.version
            );
        }

        // Pass the dereferenced vector to the downloader.
        if (!downloader.download_all(*jobs)) {
            log::error("One or more downloads failed. Aborting transaction.");
            return std::unexpected(TransactionError::DownloadFailed);
        }

        for (size_t i = 0; i < transaction.to_install.size(); ++i) {
            transaction.to_install[i].downloaded_archive_path = download_paths[i];
        }

        log::ok("All assets downloaded successfully.");

        log::info("Verifying package integrity...");

        // --- NEW: Verify Checksums ---
        if (m_skip_crypto_checks) {
            log::warn("Skipping all package integrity checks as requested.");
        } else {
            log::info("Verifying package integrity...");
            for (size_t i = 0; i < transaction.to_install.size(); ++i) {
                const auto& pkg = transaction.to_install[i].metadata;
                const auto& path = download_paths[i];

                log::progress("Verifying " + pkg.name + "...");
                if (!au::verify_file_checksum(path, pkg.checksum)) {
                    std::filesystem::remove(path);
                    log::progress_ok();
                    log::error("Integrity check failed. Aborting transaction.");
                    return std::unexpected(TransactionError::ChecksumMismatch);
                }
            }
            log::progress_ok();
        }
        log::progress_ok(); // Final "OK" for the whole verification step

        for (size_t i = 0; i < transaction.to_install.size(); ++i) {
            transaction.to_install[i].downloaded_archive_path = download_paths[i];
        }

        return {};
    }

    int PackageManager::compare_versions(const std::string& v1, const std::string& v2) const {
        std::stringstream ss1(v1);
        std::stringstream ss2(v2);
        std::string segment1, segment2;

        std::vector<int> v1_parts, v2_parts;
        while(std::getline(ss1, segment1, '.')) {
            v1_parts.push_back(std::stoi(segment1));
        }
        while(std::getline(ss2, segment2, '.')) {
            v2_parts.push_back(std::stoi(segment2));
        }

        size_t max_len = std::max(v1_parts.size(), v2_parts.size());
        v1_parts.resize(max_len, 0);
        v2_parts.resize(max_len, 0);

        for (size_t i = 0; i < max_len; ++i) {
            if (v1_parts[i] < v2_parts[i]) return -1;
            if (v1_parts[i] > v2_parts[i]) return 1;
        }

        return 0; // Versions are identical
    }

    // Helper to get the current date as a string`
    std::string get_current_date() {
        const auto now = std::chrono::system_clock::now();
        const auto in_time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d");
        return ss.str();
    }

    PackageManager::PackageManager(const std::filesystem::path& system_root, bool skip_crypto_checks)
            : m_root_path(system_root),
            // All internal paths are relative to the system root
              m_db_path(system_root / "var" / "lib" / "aurora" / "aurora.db"),
              m_cache_path(system_root / "var" / "cache" / "aurora" / "pkg"),
              m_db(m_db_path), // The Database object uses this path
            // Note: The config path for repos should also be inside the root
              m_repo_manager(m_db, system_root / "etc" / "aurora" / "repos.conf"),
              m_resolver(m_db),
              m_skip_crypto_checks(skip_crypto_checks)
    {
        // Ensure the directories for our database and cache exist
        std::filesystem::create_directories(m_db_path.parent_path());
        std::filesystem::create_directories(m_cache_path);
    }

    // Private helper to check if a dependency is met by an installed package or a provision.
    bool PackageManager::is_dependency_satisfied(const std::string& dep_name) const {
        // 1. Check if a package with the exact name is installed.
        if (m_db.is_package_installed(dep_name)) {
            return true;
        }

        // 2. Check if any installed package "provides" this dependency.
        for (const auto& installed_pkg : m_db.list_installed_packages()) {
            for (const auto& provision : installed_pkg.provides) {
                if (provision == dep_name) {
                    return true;
                }
            }
        }
        return false;
    }

    std::expected<Transaction, TransactionError> PackageManager::plan_remove_transaction(const std::vector<std::string>& package_names, bool force) {
        log::info("Planning removal transaction...");
        Transaction plan;

        // 1. Get a master list of all installed packages for efficient lookups.
        const auto all_installed_pkgs = m_db.list_installed_packages();
        std::set<std::string> targets_to_remove(package_names.begin(), package_names.end());

        for (const auto& pkg_name : package_names) {
            // 2. Find the target package.
            auto target_it = std::find_if(all_installed_pkgs.begin(), all_installed_pkgs.end(),
                                          [&](const auto& p){ return p.name == pkg_name; });

            if (target_it == all_installed_pkgs.end()) {
                log::error("Cannot remove '" + pkg_name + "': package is not installed.");
                return std::unexpected(TransactionError::PackageNotInstalled);
            }
            plan.to_remove.push_back(*target_it);

            // 3. Check for reverse dependencies.
            for (const auto& other_pkg : all_installed_pkgs) {
                // Don't check a package against itself or other packages being removed in the same transaction.
                if (targets_to_remove.count(other_pkg.name)) {
                    continue;
                }

                // Check if this 'other_pkg' depends on the package we're trying to remove.
                for (const auto& dep : other_pkg.deps) {
                    if (dep == pkg_name) {
                        if (!force) {
                            log::error("Cannot remove '" + pkg_name + "': required by installed package '" + other_pkg.name + "'.");
                            return std::unexpected(TransactionError::DependencyViolation);
                        }
                    }
                }
            }
        }
        log::ok("Removal plan created successfully.");
        return plan;
    }

    std::expected<void, TransactionError> PackageManager::install_local_package(const std::filesystem::path& package_path, bool force) {
        log::info("Attempting to install local package: " + package_path.string());

        // --- Phase 1: Metadata Extraction and Parsing ---
        auto meta_content = au::extract_single_file_to_memory(package_path, ".AURORA_META");
        if (!meta_content) {
            log::error("Could not extract metadata file (.AURORA_META) from package.");
            return std::unexpected(TransactionError::ExtractionFailed);
        }

        auto parse_result = Parser::parse_from_string(*meta_content);
        if (!parse_result) {
            log::error("Could not parse metadata from package.");
            return std::unexpected(TransactionError::ResolutionFailed); // Closest error type
        }
        const Package& pkg = *parse_result;

        // --- NEW: Integrity Check ---
        log::info("Verifying local package integrity...");
        if (m_skip_crypto_checks) {
            log::warn("Skipping local package integrity check as requested.");
        } else {
            log::info("Verifying local package integrity...");
            if (!au::verify_file_checksum(package_path, pkg.checksum)) {
                return std::unexpected(TransactionError::ChecksumMismatch);
            }
            log::ok("Integrity check passed.");
        }

        // --- Phase 2: Pre-flight Checks (Read-only operations) ---
        // 1. Check if already installed
        if (m_db.is_package_installed(pkg.name)) {
            log::error("Package '" + pkg.name + "' is already installed.");
            return std::unexpected(TransactionError::PackageAlreadyInstalled);
        }

        // 2. Check dependencies
        for (const auto& dep : pkg.deps) {
            if (!is_dependency_satisfied(dep)) {
                if (!force) {
                    log::error("Unsatisfied dependency for '" + pkg.name + "': " + dep);
                    return std::unexpected(TransactionError::ResolutionFailed);
                }
            }
        }

        // 3. Check conflicts
        for (const auto& conflict : pkg.conflicts) {
            if (m_db.is_package_installed(conflict)) {
                if (!force) {
                    log::error("Conflict detected: '" + pkg.name + "' conflicts with installed package '" + conflict + "'.");
                    return std::unexpected(TransactionError::FileConflict); // Using this for package conflicts
                }
            }
        }

        // --- Phase 3: Build the Transaction Plan ---
        Transaction plan;

        // 4. Handle replaces
        for (const auto& replace_target : pkg.replaces) {
            auto target_pkg = m_db.get_installed_package(replace_target);
            if (target_pkg) {
                log::info("Package '" + pkg.name + "' replaces '" + replace_target + "'. It will be removed.");
                plan.to_remove.push_back(*target_pkg);
            }
        }

        // 5. Add the primary package to the installation plan
        plan.to_install.push_back({pkg, package_path});

        // --- Phase 4: Execute the Transaction ---
        // We can reuse our robust, transactional execution logic!
        log::ok("Pre-flight checks passed. Executing transaction.");
        return execute_transaction(plan);
    }

std::expected<void, TransactionError> PackageManager::execute_transaction(const Transaction& plan) {
    if (plan.is_empty()) {
        return {};
    }

    // --- PHASE 0: INITIALIZATION & LOCKING ---
    // A real implementation should acquire a system-wide lock here (e.g., a PID file)
    // to prevent concurrent package manager operations.

    const auto tx_id = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    const auto tx_workspace = m_cache_path / "tx" / tx_id;
    const auto tx_backup_dir = tx_workspace / "backup";
    std::filesystem::create_directories(tx_backup_dir);

    FileSystemJournal journal;
        std::map<std::string, std::filesystem::path> post_remove_scripts;

    log::info("Executing transaction " + tx_id + "...");

    try {
        const bool use_chroot = (m_root_path != "/");

        // --- PHASE 1: PREPARE & BACKUP (The "Undo" Log) ---
        // This is the most critical change. We are moving old files, not deleting them.
        // A 'move' operation on the same filesystem is atomic.
        log::progress("Backing up existing files...");
        for (const auto& pkg_to_remove : plan.to_remove) {
            for (const auto& file : pkg_to_remove.owned_files) {
                const auto source_path = m_root_path / file;
                const auto backup_path = tx_backup_dir / file;

                if (std::filesystem::exists(source_path) || std::filesystem::is_symlink(source_path)) {
                    std::filesystem::create_directories(backup_path.parent_path());
                    std::filesystem::rename(source_path, backup_path); // Atomic move
                    journal.old_files_backed_up[source_path] = backup_path;
                }
            }
        }
        log::progress_ok();

        log::progress("Running pre-remove scripts...");
        for (const auto& pkg_to_remove : plan.to_remove) {
            if (!pkg_to_remove.pre_remove_script.empty()) {
                const auto script_path = m_root_path / pkg_to_remove.pre_remove_script;
                if (std::filesystem::exists(script_path)) {
                    if (!m_lua_sandbox.run_script_from_file(script_path, m_root_path)) {
                        std::string msg = "Pre-remove script for " + pkg_to_remove.name + " failed.";
                        log::error("\n" + msg);
                        throw TransactionException(TransactionError::ScriptletFailed, msg);
                    }
                }
            }
        }
        log::progress_ok();

        // --- PHASE 2: STAGE NEW FILES ---
        log::progress("Installing packages...");
        std::vector<InstalledPackage> completed_installs; // To hold metadata for the DB commit
        for (const auto& install_item : plan.to_install) {
            const auto& pkg = install_item.metadata;
            const auto staging_path = m_cache_path / "staging" / pkg.name;

            // 1. Cleanly extract the package to its own staging area.
            std::filesystem::remove_all(staging_path);
            std::filesystem::create_directories(staging_path);
            auto extract_result = au::extract(install_item.downloaded_archive_path, staging_path);
            if (!extract_result) {
                std::string msg = "Failed to extract archive for " + pkg.name;
                log::error("\n" + msg);
                throw TransactionException(TransactionError::ExtractionFailed, msg);
            }

            // 2. Run the pre-install script FROM the staging area.
            // This runs before the package's files are on the live system.
            if (!pkg.pre_install_script.empty()) {
                const auto script_path_in_stage = staging_path / pkg.pre_install_script;
                if (!m_lua_sandbox.run_script_from_file(script_path_in_stage, m_root_path)) {
                    log::error("\nPre-install script for " + pkg.name + " failed.");
                    throw TransactionException(TransactionError::ScriptletFailed, "Pre-install script failed.");
                }
            }

            // 3. Move the extracted files to their final destination.
            for (const auto& file : *extract_result) {
                const auto source_path = staging_path / file;
                const auto dest_path = m_root_path / file;

                if (std::filesystem::exists(dest_path)) {
                    std::string msg = "\nFile conflict during execution: " + dest_path.string();
                    log::error("\n" + msg);
                    throw TransactionException(TransactionError::FileConflict, msg);
                }

                std::filesystem::create_directories(dest_path.parent_path());
                std::filesystem::rename(source_path, dest_path); // Atomic move
                journal.new_files_committed.push_back(dest_path);
            }
            std::filesystem::remove_all(staging_path);

            // 4. Prepare metadata for the final database commit.
            InstalledPackage final_package;
            static_cast<Package&>(final_package) = pkg;
            final_package.install_date = get_current_date();
            final_package.owned_files = *extract_result;
            completed_installs.push_back(std::move(final_package));
        }
        log::progress_ok();

        // --- PHASE 3: DATABASE COMMIT (The Point of No Return) ---
        log::progress("Committing changes to database...");

        // First, collect the names of packages to be removed for the transaction
        std::vector<std::string> names_to_remove;
        names_to_remove.reserve(plan.to_remove.size());
        for (const auto& pkg_to_remove : plan.to_remove) {
            names_to_remove.push_back(pkg_to_remove.name);
        }

        // Now, call our new, atomic database method.
        // 'completed_installs' is the vector of new packages to add.
        if (!m_db.perform_transactional_update(completed_installs, names_to_remove)) {
            std::string msg = "Database commit failed. Initiating filesystem rollback.";
            log::error("\n" + msg);
            throw TransactionException(TransactionError::FileSystemError, msg);
        }
        log::progress_ok();

        // --- POST-TRANSACTION HOOKS ---
        // This happens after the DB commit. The system state is now officially updated.
        // A failure in a post-script is a WARNING, it should NOT roll back the transaction.
        log::progress("Running post-transaction hooks...");

        // 1. Run post-install scripts for newly installed packages.
        for (const auto& installed_pkg : completed_installs) {
            if (!installed_pkg.post_install_script.empty()) {
                const auto script_path = m_root_path / installed_pkg.post_install_script;
                if (std::filesystem::exists(script_path)) {
                    if (!m_lua_sandbox.run_script_from_file(script_path, m_root_path)) {
                        // Log as a warning, but do not throw. The package is installed.
                        log::warn("\nPost-install script for " + installed_pkg.name + " failed.");
                    }
                }
            }
        }

        // 2. Run post-remove scripts for removed packages.
        for (const auto& pkg_to_remove : plan.to_remove) {
            if (!pkg_to_remove.post_remove_script.empty()) {
                // The script file no longer exists on the root filesystem; it's in our backup.
                const auto script_path = tx_backup_dir / pkg_to_remove.post_remove_script;
                if (std::filesystem::exists(script_path)) {
                    if (!m_lua_sandbox.run_script_from_file(script_path, m_root_path)) {
                        log::warn("\nPost-remove script for " + pkg_to_remove.name + " failed.");
                    }
                }
            }
        }
        log::progress_ok(); // Finish the progress line for all hooks


    } catch (const TransactionException& e) {
        // --- ROLLBACK ---
        // Use e.what() to get the descriptive message for logging.
        log::error("Transaction failed: " + std::string(e.what()) + ". Rolling back filesystem changes...");

        rollback_transaction(journal, tx_workspace);

        log::ok("Rollback complete. System restored to original state.");
        std::filesystem::remove_all(tx_workspace);

        // Use e.get_error() to get the enum for the std::unexpected return.
        return std::unexpected(e.get_error());
    }

    // --- PHASE 4: CLEANUP (Success) ---
    // The transaction succeeded. We can now safely delete the backups.
    log::progress("Cleaning up transaction workspace...");
    std::filesystem::remove_all(tx_workspace);
    log::progress_ok();
    // Release system-wide lock here

    log::ok("Transaction completed successfully.");
    return {};
}

    // The high-level install function that ties the plan-prepare-execute sequence together
    std::expected<void, TransactionError> PackageManager::install(const std::vector<std::string>& package_names, bool force) {
        // 1. Plan
        auto plan_result = plan_install_transaction(package_names, force);
        if (!plan_result) {
            return std::unexpected(plan_result.error());
        }
        Transaction plan = std::move(*plan_result);

        if (plan.is_empty()) {
            log::info("Nothing to do. All packages are already installed.");
            return {};
        }

        log::info("Checking available disk space...");

        // A. Calculate installation size delta
        int64_t install_delta = 0;
        for(const auto& item : plan.to_install) {
            install_delta += item.metadata.installed_size;
        }
        for(const auto& pkg : plan.to_remove) {
            // CORRECTED LINE: Access installed_size directly on the InstalledPackage object.
            install_delta -= pkg.installed_size;
        }

        // B. Get download size dynamically
        Downloader downloader; // We need an instance to call the method
        std::vector<DownloadJob> jobs;
        for(const auto& pkg : plan.to_install) {
            auto urls_opt = m_repo_manager.get_repo_urls(pkg.metadata.repo_name);
            if(urls_opt) jobs.emplace_back(*urls_opt, std::filesystem::path{}, "");
        }
        int64_t download_size = downloader.get_total_download_size(jobs);

        // C. Perform checks
        std::error_code ec;
        auto cache_space = std::filesystem::space(m_cache_path, ec);
        if (!ec && download_size > 0 && cache_space.available < static_cast<uint64_t>(download_size)) {
            log_space_error(m_cache_path.string(), download_size, cache_space.available);
            return std::unexpected(TransactionError::NotEnoughSpace);
        }

        auto root_space = std::filesystem::space(m_root_path, ec);
        if (!ec && install_delta > 0 && root_space.available < static_cast<uint64_t>(install_delta)) {
            log_space_error(m_root_path.string(), install_delta, root_space.available);
            return std::unexpected(TransactionError::NotEnoughSpace);
        }
        log::ok("Disk space check passed.");

        auto prepare_result = prepare_transaction_assets(plan);
        if (!prepare_result) {
            return std::unexpected(prepare_result.error());
        }

        // 3. Execute
        return execute_transaction(plan);

    }

    std::expected<Transaction, TransactionError> PackageManager::plan_install_transaction(const std::vector<std::string>& package_names, bool force) {
        log::info("Planning installation transaction...");

        // 1. Resolve dependencies to get a topologically sorted list.
        auto resolved_result = m_resolver.resolve(package_names);
        if (!resolved_result) {
            log::error("Dependency resolution failed.");
            return std::unexpected(TransactionError::ResolutionFailed);
        }
        ResolutionList packages_to_install = *resolved_result;

        if (packages_to_install.empty()) {
            return Transaction{}; // Nothing to do, return empty plan
        }

        // --- 2. Perform File Conflict Check ---
                log::info("Checking for file conflicts...");

        // Build a master map of all files owned by currently installed packages.
        std::map<std::filesystem::path, std::string> all_owned_files;
        for (const auto& installed_pkg : m_db.list_installed_packages()) {
            for (const auto& file : installed_pkg.owned_files) {
                all_owned_files[file] = installed_pkg.name;
            }
        }

        // Now, check each file from each new package against the map AND the filesystem.
        for (const auto& pkg_meta : packages_to_install) {
            for (const auto& new_file : pkg_meta.files) {
                const auto& owner_lookup = all_owned_files.find(new_file);

                // STAGE 1: Check if the file is owned by another package in the DB.
                if (owner_lookup != all_owned_files.end()) {
                    // Conflict found with an owned file.
                    if (!force) {
                        const std::string& owner_pkg_name = owner_lookup->second;
                        log::error("File conflict: Package '" + pkg_meta.name +
                                   "' wants to install '" + new_file.string() +
                                   "', which is already owned by '" + owner_pkg_name + "'.");
                        return std::unexpected(TransactionError::FileConflict);
                    }
                }
                // STAGE 2: Check for unowned files on the live filesystem.
                else {
                    const auto path_on_disk = m_root_path / new_file;
                    // We must check for both regular files and symlinks.
                    if (std::filesystem::exists(path_on_disk) || std::filesystem::is_symlink(path_on_disk)) {
                        // The file exists on disk but is not in our ownership map.
                        if (!force) {
                             log::error("File conflict: Package '" + pkg_meta.name +
                                       "' wants to install '" + new_file.string() +
                                       "', which already exists on the filesystem and is not owned by any package.");
                             return std::unexpected(TransactionError::FileConflict);
                        }
                    }
                }
            }
        }
        log::ok("No file conflicts found.");

        // --- 3. Build the final Transaction Plan ---
        Transaction plan;

        // --- NEW LOGIC: Check for Conflicts and handle Replaces ---
        for (const auto& pkg_meta : packages_to_install) {
            // 1. Check for conflicts against currently installed packages.
            for (const auto& conflict_name : pkg_meta.conflicts) {
                if (m_db.is_package_installed(conflict_name)) {
                    if (!force) {
                        log::error("Conflict detected: package '" + pkg_meta.name +
                                   "' conflicts with installed package '" + conflict_name + "'.");
                        return std::unexpected(TransactionError::ConflictDetected);
                    }
                }
            }

            // 2. Handle 'replaces' by adding the target to the removal list.
            for (const auto& replace_name : pkg_meta.replaces) {
                auto target_pkg_opt = m_db.get_installed_package(replace_name);
                if (target_pkg_opt) {
                    // To avoid duplicates, check if it's already in the to_remove list
                    if (std::find_if(plan.to_remove.begin(), plan.to_remove.end(),
                                     [&](const auto& p){ return p.name == replace_name; }) == plan.to_remove.end()) {
                        log::info("Package '" + pkg_meta.name + "' replaces '" + replace_name + "', scheduling it for removal.");
                        plan.to_remove.push_back(*target_pkg_opt);
                    }
                }
            }
        }
        // --- END NEW LOGIC ---

        for (const auto& pkg_meta : packages_to_install) {
            PackageInstallation install_item;
            install_item.metadata = pkg_meta;
            // The `downloaded_archive_path` will be filled in by `prepare_transaction_assets`.
            install_item.downloaded_archive_path = m_cache_path / (pkg_meta.name + "-" + pkg_meta.version + ".pkg.tar.zst");
            plan.to_install.push_back(std::move(install_item));
        }

        log::ok("Transaction plan created successfully.");
        return plan;
    }

    std::expected<void, TransactionError> PackageManager::remove(const std::vector<std::string>& package_names, bool force) {
        // 1. Plan
        auto plan_result = plan_remove_transaction(package_names, force);
        if (!plan_result) {
            return std::unexpected(plan_result.error());
        }
        Transaction plan = std::move(*plan_result);

        if (plan.is_empty()) {
            log::info("Nothing to do.");
            return {};
        }

        // 2. Execute
        return execute_transaction(plan);
    }

    std::expected<Transaction, TransactionError> PackageManager::plan_update_transaction(bool force) {
        log::info("Planning system update...");

        if (!m_repo_manager.update_all()) {
            log::error("Could not update repositories. Aborting system update.");
            return std::unexpected(TransactionError::ResolutionFailed);
        }

        Transaction plan;
        // Use a map to build the final install list, ensuring no duplicates and easy access.
        std::map<std::string, Package> targets_to_install;
        std::vector<std::string> new_dependencies_to_resolve;

        // 1. Find packages that have a newer version in the repositories.
        for (const auto& installed_pkg : m_db.list_installed_packages()) {
            auto repo_pkg_opt = m_db.find_repo_package(installed_pkg.name);
            if (repo_pkg_opt && compare_versions(repo_pkg_opt->version, installed_pkg.version) > 0) {
                log::info("Upgrade found for " + installed_pkg.name + ": " +
                          installed_pkg.version + " -> " + repo_pkg_opt->version);

                // A. Add the old version to the removal list.
                plan.to_remove.push_back(installed_pkg);

                // B. Add the NEW version to our map of targets.
                targets_to_install[repo_pkg_opt->name] = *repo_pkg_opt;

                // C. Gather all dependencies of the NEW version for the resolver.
                for (const auto& dep : repo_pkg_opt->deps) {
                    new_dependencies_to_resolve.push_back(dep);
                }
            }
        }

        if (targets_to_install.empty()) {
            log::ok("System is already up to date.");
            return plan; // Return empty plan
        }

        // 2. Resolve only the dependencies of the new packages.
        log::info("Resolving dependencies for updated packages...");
        auto resolved_deps_result = m_resolver.resolve(new_dependencies_to_resolve);
        if (!resolved_deps_result) {
            log::error("Dependency resolution failed for updates.");
            return std::unexpected(TransactionError::ResolutionFailed);
        }
        // Add the resolved dependencies to our map of targets.
        for (const auto& resolved_dep : *resolved_deps_result) {
            targets_to_install[resolved_dep.name] = resolved_dep;
        }

        // 4. Perform file conflict check and build the final installation list.
        std::set<std::string> removing_pkg_names;
        for(const auto& p : plan.to_remove) { removing_pkg_names.insert(p.name); }

        std::map<std::filesystem::path, std::string> all_owned_files;
        for (const auto& installed_pkg : m_db.list_installed_packages()) {
            if (removing_pkg_names.count(installed_pkg.name)) continue; // Ignore packages being removed
            for (const auto& file : installed_pkg.owned_files) {
                all_owned_files[file] = installed_pkg.name;
            }
        }

        for (const auto& [name, pkg_meta] : targets_to_install) {
            for (const auto& new_file : pkg_meta.files) {
                if (all_owned_files.count(new_file)) {
                    log::error("File conflict detected on update: " + new_file.string());
                    return std::unexpected(TransactionError::FileConflict);
                }
            }
            plan.to_install.push_back({pkg_meta, m_cache_path / (pkg_meta.name + "-" + pkg_meta.version + ".pkg.tar.zst")});
        }

        log::ok("System update plan created successfully.");
        return plan;
    }


    std::expected<void, TransactionError> PackageManager::update_system(bool force) {
        // 1. Plan the transaction.
        auto plan_result = plan_update_transaction();
        if (!plan_result) {
            return std::unexpected(plan_result.error());
        }
        Transaction plan = std::move(*plan_result);

        if (plan.is_empty()) {
            return {}; // Nothing to do
        }

        auto prepare_result = prepare_transaction_assets(plan);
        if (!prepare_result) {
            return std::unexpected(prepare_result.error());
        }

            // 3. Execute the transaction.
            // Our existing execute_transaction is already designed to handle
            // removals and installations in the same transaction, making this step simple.
            return execute_transaction(plan);

    }

    bool PackageManager::sync_database() {
        log::info("Syncing repositories to local database...");
        return m_repo_manager.update_all(m_skip_crypto_checks);
    }

    void PackageManager::rollback_transaction(const FileSystemJournal& journal, const std::filesystem::path& tx_workspace) {
        // 1. Undo new file installations by removing them.
        // We iterate in reverse in case there were directory creations.
        for (const auto & it : std::ranges::reverse_view(journal.new_files_committed)) {
            std::error_code ec;
            std::filesystem::remove(it, ec);
        }

        // 2. Restore backed-up files by moving them back.
        for (const auto& [original_path, backup_path] : journal.old_files_backed_up) {
            if (std::filesystem::exists(backup_path)) {
                // Ensure parent directory exists before moving back
                if (original_path.has_parent_path()) {
                    std::filesystem::create_directories(original_path.parent_path());
                }
                std::filesystem::rename(backup_path, original_path);
            }
        }
    }


} // namespace au