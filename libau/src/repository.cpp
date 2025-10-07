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

#include "libau/crypto.h"

namespace au {


    struct Repository {
        std::string name;
        std::string url;
    };

    struct RepositoryManager::Impl {
        Database& db;
        std::filesystem::path config_path;
        std::map<std::string, std::vector<std::string>> repositories;

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
            std::string current_repo;
            while (std::getline(config_file, line)) {
                // Trim leading whitespace
                line.erase(0, line.find_first_not_of(" \t"));

                if (line.empty() || line[0] == '#') { // Skip empty lines and comments
                    continue;
                }

                if (line.front() == '[' && line.back() == ']') { // New repository section
                    current_repo = line.substr(1, line.size() - 2);
                    repositories[current_repo] = {}; // Initialize an empty vector
                } else if (!current_repo.empty()) { // URL line for the current repo
                    auto pos = line.find('=');
                    if (pos != std::string::npos) {
                        std::string key = line.substr(0, pos);
                        key.erase(key.find_last_not_of(" \t") + 1); // trim right
                        if (key == "url") {
                            std::string value = line.substr(pos + 1);
                            value.erase(0, value.find_first_not_of(" \t")); // trim left
                            repositories[current_repo].push_back(value);
                        }
                    }
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
    bool RepositoryManager::update_all(bool skip_gpg_check) {
    pimpl->load_config(); // Re-read config in case it changed

    std::vector<Package> all_packages;
    bool all_succeeded = true;

    // Create a single downloader instance to be used for all repo files.
    Downloader downloader;

    // The `repositories` map is now a std::map<std::string, std::vector<std::string>>
    for (const auto& [repo_name, mirror_urls] : pimpl->repositories) {
        log::info("Updating repository '" + repo_name + "'...");

        if (mirror_urls.empty()) {
            log::warn("Repository '" + repo_name + "' has no mirrors defined. Skipping.");
            continue;
        }

        // --- 1. Prepare mirror URLs for both the index and signature files ---
        std::vector<std::string> index_urls;
        std::vector<std::string> sig_urls;
        index_urls.reserve(mirror_urls.size());
        sig_urls.reserve(mirror_urls.size());

        for (const auto& base_url : mirror_urls) {
            index_urls.push_back(base_url + "/repo.yaml");
            sig_urls.push_back(base_url + "/repo.yaml.sig");
        }

        // --- 2. Create download jobs ---
        const auto temp_path = std::filesystem::temp_directory_path() / (repo_name + ".yaml.tmp");
        const auto temp_sig_path = std::filesystem::temp_directory_path() / (repo_name + ".yaml.sig.tmp");

        std::vector<DownloadJob> jobs;
        jobs.emplace_back(std::move(index_urls), temp_path, "index: " + repo_name);

        if (!skip_gpg_check) {
            jobs.emplace_back(std::move(sig_urls), temp_sig_path, "sig: " + repo_name);
        }

        // --- 3. Execute the downloads (the downloader will handle mirror fallback) ---
        if (!downloader.download_all(jobs)) {
            log::error("Failed to download index/signature for repo '" + repo_name + "'.");
            all_succeeded = false;
            // Clean up any partial downloads before skipping to the next repo
            std::filesystem::remove(temp_path);
            std::filesystem::remove(temp_sig_path);
            continue;
        }

        // --- 4. GPG Verification (if not skipped) ---
        if (skip_gpg_check) {
            log::warn("Skipping GPG authenticity check for repository '" + repo_name + "'.");
        } else {
            if (!au::verify_repository_signature(temp_path, temp_sig_path)) {
                log::error("Repository '" + repo_name + "' failed authenticity check. Skipping.");
                all_succeeded = false;
                std::filesystem::remove(temp_path);
                std::filesystem::remove(temp_sig_path);
                continue;
            }
            log::ok("Repository '" + repo_name + "' authenticity verified.");
        }

        // --- 5. Parse the now-trusted index file ---
        auto parse_result = Parser::parse_repository_index(temp_path);

        // --- 6. Clean up temporary files ---
        std::filesystem::remove(temp_path);
        if (!skip_gpg_check) {
            std::filesystem::remove(temp_sig_path);
        }

        if (!parse_result) {
            log::error("Failed to parse index for repo '" + repo_name + "'.");
            all_succeeded = false;
            continue;
        }

        // --- 7. Tag packages with their origin repo and add to master list ---
        for (auto& parsed_pkg : *parse_result) {
            parsed_pkg.repo_name = repo_name;
        }
        all_packages.insert(all_packages.end(),
                            std::make_move_iterator(parse_result->begin()),
                            std::make_move_iterator(parse_result->end()));
    }

    // --- 8. Final database sync ---
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


    std::optional<std::vector<std::string>> RepositoryManager::get_repo_urls(const std::string& repo_name) const {
        auto it = pimpl->repositories.find(repo_name);
        if (it != pimpl->repositories.end() && !it->second.empty()) {
            return it->second;
        }
        return std::nullopt;
    }

} // namespace au