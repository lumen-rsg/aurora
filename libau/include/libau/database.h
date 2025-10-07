//
// Created by cv2 on 8/14/25.
//

#pragma once

#include "package.h"
#include <vector>
#include <optional>
#include <filesystem>

// Forward declare the ORM storage type
namespace sqlite_orm { template<class... Ts> class storage_t; }

namespace au {

    class Database {
    public:
        explicit Database(const std::filesystem::path& db_path);
        ~Database();

        // --- Installed Packages Table ---
        void add_installed_package(const InstalledPackage& pkg);
        void remove_installed_package(const std::string& name);
        std::optional<InstalledPackage> get_installed_package(const std::string& name) const;
        bool is_package_installed(const std::string& name) const;
        std::vector<InstalledPackage> list_installed_packages() const;

        // --- Repository Packages Table (the "available" packages) ---
        void sync_repo_packages(const std::vector<Package>& packages);
        std::optional<Package> find_repo_package(const std::string& name) const;
        std::vector<Package> list_all_repo_packages() const;

        // Atomically updates the database as a single transaction.
        // Returns true on success, false on failure (rollback).
        bool perform_transactional_update(
            const std::vector<InstalledPackage>& packages_to_add,
            const std::vector<std::string>& package_names_to_remove
        );

    private:
        // PIMPL idiom to hide the sqlite_orm implementation details
        struct Impl;
        std::unique_ptr<Impl> pimpl;
    };

} // namespace au