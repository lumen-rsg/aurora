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
#include <map>

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
        ConflictDetected,
        DependencyViolation,
        NotEnoughSpace,
        AmbiguousProvider
    };

    class TransactionException : public std::runtime_error {
    public:
        // Constructor takes the error enum and a descriptive message.
        TransactionException(TransactionError error, const std::string& what_arg)
            : std::runtime_error(what_arg), m_error(error) {}

        // A getter to retrieve the specific error code.
        TransactionError get_error() const {
            return m_error;
        }

    private:
        TransactionError m_error;
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
        explicit PackageManager(const std::filesystem::path& system_root = "/", bool skip_crypto_checks = false);

        // High-level operations that create and execute a transaction.
        std::expected<void, TransactionError> install(const std::vector<std::string>& package_names, bool force = false);
        std::expected<void, TransactionError> remove(const std::vector<std::string>& package_names, bool force = false);

        // --- Core Transactional Methods ---

        // 2. Prepare: Download and verify all needed files for the transaction.
        std::expected<void, TransactionError> prepare_transaction_assets(Transaction& transaction);

        // 3. Execute: Run scripts, extract files, update DB. This should be atomic.
        std::expected<void, TransactionError> execute_transaction(const Transaction& plan);

        // NEW: Install a single local package file.
        std::expected<void, TransactionError> update_system(bool force = false);
        std::expected<void, TransactionError> install_local_package(const std::filesystem::path& package_path, bool force = false);
        bool sync_database();

        std::expected<Transaction, TransactionError> plan_install_transaction(const std::vector<std::string>& package_names, bool force = false);
        std::expected<Transaction, TransactionError> plan_remove_transaction(const std::vector<std::string>& package_names, bool force = false);
        std::expected<Transaction, TransactionError> plan_update_transaction(bool force = false);

    private:
        std::filesystem::path m_root_path; // The target system's root directory
        std::filesystem::path m_db_path;
        std::filesystem::path m_cache_path; // For downloaded packages
        bool m_skip_crypto_checks;

        Database m_db;
        RepositoryManager m_repo_manager;
        DependencyResolver m_resolver;
        bool is_dependency_satisfied(const std::string& dep_name) const;
        bool run_pre_script(const std::filesystem::path& script_path, const std::filesystem::path& target_root) const;
        bool run_post_script(const std::filesystem::path& script_path_in_root, bool use_chroot) const;

        int compare_versions(const std::string& v1, const std::string& v2) const;

        struct FileSystemJournal {
            std::vector<std::filesystem::path> new_files_committed;
            std::map<std::filesystem::path, std::filesystem::path> old_files_backed_up;
        };

        void rollback_transaction(const FileSystemJournal& journal, const std::filesystem::path& tx_workspace);
    };

} // namespace au