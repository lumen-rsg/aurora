//
// Created by cv2 on 8/14/25.
//

#include "libau/repository.h"
#include "libau/database.h"
#include "libau/downloader.h"
#include "libau/parser.h"
#include "libau/logging.h"

#include <fstream>
#include <sstream>
#include <map>

namespace au {


    struct Repository {
        std::string name;
        std::string url;
    };

    struct RepositoryManager::Impl {
        Database& db;
        std::filesystem::path config_path;
        std::vector<Repository> repositories;

        Impl(Database& database, const std::filesystem::path& conf_path)
                : db(database), config_path(conf_path) {}

        // Reads the simple "name = url" config file
        void load_config() {
            repositories.clear();
            std::ifstream config_file(config_path);
            if (!config_file.is_open()) {
                log::error("Could not open repository config file: " + config_path.string());
                return;
            }

            std::string line;
            while (std::getline(config_file, line)) {
                std::istringstream iss(line);
                std::string name, equals, url;
                if (iss >> name >> equals >> url && equals == "=") {
                    repositories.push_back({name, url});
                }
            }
        }
    };

    // We need a PIMPL here too to hide the implementation details
    RepositoryManager::RepositoryManager(Database& db, const std::filesystem::path& config_path)
            : pimpl(std::make_unique<Impl>(db, config_path))
    {
        pimpl->load_config();
    }

    RepositoryManager::~RepositoryManager() = default;

    // The main function that orchestrates the update
    bool RepositoryManager::update_all() {
        pimpl->load_config(); // Re-read config in case it changed

        std::vector<Package> all_packages;
        bool all_succeeded = true;

        // Create a single downloader instance to be used for all repo files.
        Downloader downloader;

        for (const auto& repo : pimpl->repositories) {
            log::info("Updating repository '" + repo.name + "'...");

            const std::string index_url = repo.url + "/repo.yaml";
            const auto temp_path = std::filesystem::temp_directory_path() / (repo.name + ".yaml.tmp");

            // Create a download job for this single repository index file.
            std::vector<DownloadJob> jobs = {
                    {
                            index_url,
                            temp_path,
                            "repo: " + repo.name
                    }
            };

            // Execute the download.
            bool download_success = downloader.download_all(jobs);

            if (!download_success) {
                log::error("Failed to download index for repo '" + repo.name + "'.");
                all_succeeded = false;
                std::filesystem::remove(temp_path); // Clean up partial download
                continue; // Move to the next repo
            }

            auto parse_result = Parser::parse_repository_index(temp_path);
            std::filesystem::remove(temp_path); // Clean up temp file

            if (!parse_result) {
                log::error("Failed to parse index for repo '" + repo.name + "'.");
                all_succeeded = false;
                continue;
            }

            for (auto& parsed_pkg : *parse_result) {
                parsed_pkg.repo_name = repo.name;
            }

            all_packages.insert(all_packages.end(), parse_result->begin(), parse_result->end());
        }

        if (all_succeeded && !all_packages.empty()) {
            log::info("Syncing all repository packages to local database...");
            pimpl->db.sync_repo_packages(all_packages);
        } else if (all_packages.empty() && all_succeeded) {
            log::info("No packages found in any repository. Database not updated.");
        }

        return all_succeeded;
    }

    std::optional<Package> RepositoryManager::find_package(const std::string& package_name) const {
        // This is now just a pass-through to the database
        return pimpl->db.find_repo_package(package_name);
    }

    std::optional<std::string> RepositoryManager::get_repo_url(const std::string& repo_name) const {
        for (const auto& repo : pimpl->repositories) {
            if (repo.name == repo_name) {
                return repo.url;
            }
        }
        return std::nullopt;
    }

} // namespace au