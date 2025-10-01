//
// Created by cv2 on 8/14/25.
//

#include "libau/package_manager.h"
#include "libau/downloader.h"
#include "libau/logging.h"
#include "libau/archive.h" // Needed for extraction
#include <chrono>
#include <iomanip>
#include <sstream>
#include "libau/parser.h"
#include <map>
#include <sys/wait.h>

namespace au {

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

            auto repo_url_opt = m_repo_manager.get_repo_url(pkg.repo_name);
            if (!repo_url_opt) {
                log::error("Cannot find repository URL for repo '" + pkg.repo_name + "'.");
                return std::unexpected(TransactionError::ResolutionFailed);
            }

            std::string url = *repo_url_opt + "/" + pkg.name + "-" + pkg.version + ".au";
            download_paths.push_back(m_cache_path / (pkg.name + "-" + pkg.version + ".au"));
            jobs->emplace_back(
                    url,
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

    // --- HELPER 1: For PRE-scripts (install & remove) ---
    // Runs WITHOUT chroot, passing the root dir as an argument.
    bool PackageManager::run_pre_script(const std::filesystem::path& script_path, const std::filesystem::path& target_root) const {
        if (script_path.empty() || !std::filesystem::exists(script_path)) {
            return true;
        }
        std::filesystem::permissions(script_path, std::filesystem::perms::owner_exec, std::filesystem::perm_options::add);

        // Command: /path/to/script /path/to/target/root
        std::string command = script_path.string() + " " + target_root.string();
        log::info("Executing pre-op command: " + command);

        int status = std::system(command.c_str());
        return WEXITSTATUS(status) == 0;
    }

    // --- HELPER 2: For POST-scripts (install & remove) ---
    // Runs WITH chroot for safety, as files are now in a consistent state.
    bool PackageManager::run_post_script(const std::filesystem::path& script_path_in_root, bool use_chroot) const {
        const auto script_path_on_host = m_root_path / script_path_in_root;
        if (script_path_in_root.empty() || !std::filesystem::exists(script_path_on_host)) {
            return true;
        }
        std::filesystem::permissions(script_path_on_host, std::filesystem::perms::owner_exec, std::filesystem::perm_options::add);

        std::string command;
        const std::string script_path_for_exec = "/" + script_path_in_root.string();
        if (use_chroot) {
            command = "chroot " + m_root_path.string() + " " + script_path_for_exec;
        } else {
            command = script_path_on_host.string();
        }

        log::info("Executing post-op command: " + command);
        int status = std::system(command.c_str());
        return WEXITSTATUS(status) == 0;
    }

    // Helper to get the current date as a string`
    std::string get_current_date() {
        const auto now = std::chrono::system_clock::now();
        const auto in_time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d");
        return ss.str();
    }

    PackageManager::PackageManager(const std::filesystem::path& system_root)
            : m_root_path(system_root),
            // All internal paths are relative to the system root
              m_db_path(system_root / "var" / "lib" / "aurora" / "aurora.db"),
              m_cache_path(system_root / "var" / "cache" / "aurora" / "pkg"),
              m_db(m_db_path), // The Database object uses this path
            // Note: The config path for repos should also be inside the root
              m_repo_manager(m_db, system_root / "etc" / "aurora" / "repos.conf"),
              m_resolver(m_db)
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
        auto meta_content = au::archive::extract_single_file_to_memory(package_path, ".AURORA_META");
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
        log::info("Executing transaction...");

        std::vector<InstalledPackage> completed_installs;
        std::vector<std::filesystem::path> created_dirs; // Track created dirs for rollback

        try {
            const bool use_chroot = (m_root_path != "/");

            // --- STAGE 0: Handle Removals FIRST ---
            for (const auto& pkg_to_remove : plan.to_remove) {
                if (!pkg_to_remove.pre_remove_script.empty()) {
                    log::info("Running pre-remove script for " + pkg_to_remove.name);
                    // Pre-remove scripts run BEFORE files are touched. Use the non-chroot helper.
                    if (!run_pre_script(m_root_path / pkg_to_remove.pre_remove_script, m_root_path)) {
                        log::error("Pre-remove script for " + pkg_to_remove.name + " failed.");
                        throw TransactionError::ScriptletFailed;
                    }
                }
            }

            // Remove all files.
            std::set<std::filesystem::path> parent_dirs; // To track for empty dir removal
            for (const auto& pkg_to_remove : plan.to_remove) {
                log::info("Removing files for " + pkg_to_remove.name);
                for (const auto& file : pkg_to_remove.owned_files) {
                    const auto full_path = m_root_path / file;
                    if (std::filesystem::exists(full_path) || std::filesystem::is_symlink(full_path)) {
                        std::filesystem::remove(full_path);
                        if (full_path.has_parent_path()) {
                            parent_dirs.insert(full_path.parent_path());
                        }
                    }
                }
            }

            // Commit removals to the database.
            for (const auto& pkg_to_remove : plan.to_remove) {
                m_db.remove_installed_package(pkg_to_remove.name);
            }

            // --- STAGE 1: Pre-install Hooks & Extraction ---
            for (const auto& install_item : plan.to_install) {
                const auto& pkg = install_item.metadata;
                log::info("Installing " + pkg.name + " " + pkg.version + "...");

                const auto staging_path = m_cache_path / "staging" / pkg.name;
                std::filesystem::remove_all(staging_path);
                std::filesystem::create_directories(staging_path);

                // 1. Extract the archive to a temporary staging area.
                auto extract_result = au::archive::extract(install_item.downloaded_archive_path, staging_path);
                if (!extract_result) {
                    log::error("Failed to extract archive for " + pkg.name);
                    throw TransactionError::ExtractionFailed;
                }

                // 2. Run pre-install scriptlet from the staging area.
                if (!pkg.pre_install_script.empty()) {
                    log::info("Running pre-install script for " + pkg.name);
                    // Pre-install scripts run BEFORE files are touched. Use the non-chroot helper.
                    if (!run_pre_script(staging_path / pkg.pre_install_script, m_root_path)) {
                        log::error("Pre-install script for " + pkg.name + " failed.");
                        throw TransactionError::ScriptletFailed;
                    }
                }

                // 3. Install files from staging to the final root directory, preserving attributes
                //    and correctly handling filesystem symlinks.
                for (const auto& file : *extract_result) {
                    auto source = staging_path / file;
                    auto dest = m_root_path / file;

                    // Use `install -D` to create parent directories and copy the file in one
                    // symlink-aware operation. We also need to get the permissions from the source file.
                    // NOTE: This assumes a GNU-compatible `install` command is available in the host.

                    // Get the source file's permissions to pass them to `install`.
                    auto perms = std::filesystem::status(source).permissions();
                    int perm_val = static_cast<int>(perms);

                    // We format the permissions into an octal mode string for the -m flag.
                    std::stringstream ss;
                    ss << std::oct << (perm_val & 07777); // Mask to get relevant permission bits

                    std::string command = "cp -a " + staging_path.string() + "/. " + m_root_path.string() + "/";
                    int status = std::system(command.c_str());

                    if (WEXITSTATUS(status) != 0) {
                        log::error("Failed to copy package payload from staging for " + pkg.name);
                        throw TransactionError::FileSystemError;
                    }

                }

                // 4. Clean up the staging directory for this package
                std::filesystem::remove_all(staging_path);

                // 4. If all steps succeed for this package, prepare its final DB entry.
                InstalledPackage final_package;
                static_cast<Package&>(final_package) = pkg;
                final_package.install_date = get_current_date();
                final_package.owned_files = *extract_result;
                completed_installs.push_back(std::move(final_package));
            }

            // --- STAGE 2: Commit to Database ---
            log::info("Committing transaction to database...");
            for (const auto& installed_pkg : completed_installs) {
                m_db.add_installed_package(installed_pkg);
            }

            // Post-install scripts run AFTER files are in place. Use the chroot-aware helper.
            for (const auto& installed_pkg : completed_installs) {
                if (!installed_pkg.post_install_script.empty()) {
                    log::info("Running post-install script for " + installed_pkg.name);
                    if (!run_post_script(installed_pkg.post_install_script, use_chroot)) {
                        log::error("Warning: Post-install script for " + installed_pkg.name + " failed.");
                    }
                }
            }
            // Post-remove scripts run AFTER files are gone. Use the chroot-aware helper.
            for (const auto& pkg_to_remove : plan.to_remove) {
                if (!pkg_to_remove.post_remove_script.empty()) {
                    log::info("Running post-remove script for " + pkg_to_remove.name);
                    if (!run_post_script(pkg_to_remove.post_remove_script, use_chroot)) {
                        log::error("Warning: Post-remove script for " + pkg_to_remove.name + " failed.");
                    }
                }
            }

            // Final cleanup: try to remove now-empty directories.
            for(auto it = parent_dirs.rbegin(); it != parent_dirs.rend(); ++it) {
                if (std::filesystem::exists(*it) && std::filesystem::is_empty(*it)) {
                    std::filesystem::remove(*it);
                }
            }


        } catch (const TransactionError& err) {
            // --- ROLLBACK STAGE --- (This is re-thrown at the end)
            log::error("A transaction error occurred. Rolling back changes...");

            for (auto it = completed_installs.rbegin(); it != completed_installs.rend(); ++it) {
                log::info("Rolling back files for " + it->name);
                for (const auto& file_to_remove : it->owned_files) {
                    std::error_code ec;
                    std::filesystem::remove(m_root_path / file_to_remove, ec);
                    if (ec) log::error("Failed to remove file on rollback: " + (m_root_path / file_to_remove).string());
                }
            }
            // Attempt to remove created directories (best-effort)
            for(auto it = created_dirs.rbegin(); it != created_dirs.rend(); ++it) {
                if (std::filesystem::is_empty(*it)) {
                    std::filesystem::remove(*it);
                }
            }

            log::ok("Rollback complete.");
            return std::unexpected(err);

        } catch (std::exception &e) {
            log::error(e.what());
            log::error("An unknown exception occurred during installation. Partial rollback may be needed.");
            // We don't have enough info to do a safe file rollback here.
            return std::unexpected(TransactionError::FileSystemError);
        }

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

        // First, build a master map of all files owned by currently installed packages.
        // This is much more efficient than repeatedly querying the database.
        std::map<std::filesystem::path, std::string> all_owned_files;
        for (const auto& installed_pkg : m_db.list_installed_packages()) {
            for (const auto& file : installed_pkg.owned_files) {
                all_owned_files[file] = installed_pkg.name;
            }
        }

        // Now, check each file from each new package against the master map.
        for (const auto& pkg_meta : packages_to_install) {
            for (const auto& new_file : pkg_meta.files) {
                auto it = all_owned_files.find(new_file);
                if (it != all_owned_files.end()) {
                    // Conflict found!
                    if (!force) {
                        const std::string& owner_pkg_name = it->second;
                        log::error("File conflict detected! Package '" + pkg_meta.name +
                                   "' wants to install '" + new_file.string() +
                                   "', which is already owned by '" + owner_pkg_name + "'.");
                        return std::unexpected(TransactionError::FileConflict);
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
        return m_repo_manager.update_all();
    }


} // namespace au