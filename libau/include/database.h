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

    private:
        // PIMPL idiom to hide the sqlite_orm implementation details
        struct Impl;
        std::unique_ptr<Impl> pimpl;
    };

} // namespace au