//
// Created by cv2 on 8/14/25.
//

#pragma once

#include "database.h"
#include "repository.h"
#include "dependency_resolver.h"

#include <vector>
#include <string>
#include <expected>
#include <filesystem>

#pragma once

#include "database.h"
#include "repository.h"
#include "dependency_resolver.h"

#include <vector>
#include <string>
#include <expected>
#include <filesystem>

namespace au {

    enum class TransactionError {
        // Planning/Preparation Errors
        ResolutionFailed,
        DownloadFailed,
        ChecksumMismatch,
        PackageAlreadyInstalled,
        PackageNotInstalled,
        // Execution Errors
        FileConflict, // A file to be installed is already owned by another package
        ExtractionFailed,
        ScriptletFailed,
        FileSystemError,
        DependencyViolation // NEW: For reverse dependency check
    };

// Represents one package to be installed in a transaction.
    struct PackageInstallation {
        Package metadata;
        std::filesystem::path downloaded_archive_path;
    };

// Represents the complete plan of action for a system change.
    struct Transaction {
        std::vector<PackageInstallation> to_install;
        std::vector<InstalledPackage> to_remove;
        // std::vector<std::pair<Package, InstalledPackage>> to_upgrade;

        bool is_empty() const { return to_install.empty() && to_remove.empty(); }
    };

    class PackageManager {
    public:
        // The system_root is crucial for bootstrapping and chroot environments.
        explicit PackageManager(const std::filesystem::path& system_root = "/");

        // High-level operations that create and execute a transaction.
        std::expected<void, TransactionError> install(const std::vector<std::string>& package_names);
        std::expected<void, TransactionError> remove(const std::vector<std::string>& package_names);

        // --- Core Transactional Methods ---

        // 1. Plan: Resolve dependencies, figure out what to download.
        std::expected<Transaction, TransactionError> plan_install_transaction(const std::vector<std::string>& package_names);

        // 2. Prepare: Download and verify all needed files for the transaction.
        std::expected<void, TransactionError> prepare_transaction_assets(Transaction& transaction);

        // 3. Execute: Run scripts, extract files, update DB. This should be atomic.
        std::expected<void, TransactionError> execute_transaction(const Transaction& plan);

        // NEW: Install a single local package file.
        std::expected<void, TransactionError> install_local_package(const std::filesystem::path& package_path);
        std::expected<void, TransactionError> update_system();
        bool sync_database();

        std::expected<Transaction, TransactionError> plan_update_transaction();

        std::expected<Transaction, TransactionError> plan_remove_transaction(const std::vector<std::string>& package_names);

    private:
        std::filesystem::path m_root_path; // The target system's root directory
        std::filesystem::path m_db_path;
        std::filesystem::path m_cache_path; // For downloaded packages

        Database m_db;
        RepositoryManager m_repo_manager;
        DependencyResolver m_resolver;
        bool is_dependency_satisfied(const std::string& dep_name) const;
        bool run_pre_script(const std::filesystem::path& script_path, const std::filesystem::path& target_root) const;
        bool run_post_script(const std::filesystem::path& script_path_in_root, bool use_chroot) const;

        int compare_versions(const std::string& v1, const std::string& v2) const;
    };

} // namespace au