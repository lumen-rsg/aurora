//
// Created by cv2 on 8/14/25.
//

#pragma once

#include "package.h"
#include <vector>
#include <string>
#include <optional>
#include <memory>
#include <filesystem>

namespace au {

    class Database;

    class RepositoryManager {
    public:
        explicit RepositoryManager(Database& db, const std::filesystem::path& config_path);
        ~RepositoryManager();

        // Fetches repository indexes, parses them, and syncs to the database.
        bool update_all(bool skip_gpg_check = false);

        // Finds a package by name across all configured repositories.
        std::optional<Package> find_package(const std::string& package_name) const;
        std::optional<std::string> get_repo_url(const std::string& repo_name) const; // NEW

    private:
        struct Impl;
        std::unique_ptr<Impl> pimpl;
    };

} // namespace au