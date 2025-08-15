//
// Created by cv2 on 8/16/25.
//
#include <iostream>
#include <filesystem>
#include <vector>
#include <fstream>
#include <algorithm>

// Include our library headers
#include <logging.h>
#include <parser.h>
#include <archive.h>
#include <package.h>

// Include our new YAML writer and the yaml-cpp library
#include "yaml_writer.h"
#include <yaml-cpp/yaml.h>

void print_usage() {
    std::cerr << "aurora-repotool - A tool for managing Aurora package repositories.\n\n"
              << "Usage:\n"
              << "  repotool init <repo_directory>\n"
              << "  repotool add <repo_directory> <package_file.pkg.tar.zst>\n"
              << "  repotool remove <repo_directory> <package_name>\n";
}

// Loads all package metadata from a repo.yaml file.
std::vector<au::Package> load_repo_db(const std::filesystem::path& repo_yaml_path) {
    if (!std::filesystem::exists(repo_yaml_path)) {
        return {}; // Return empty vector if the DB doesn't exist yet.
    }
    auto parse_result = au::Parser::parse_repository_index(repo_yaml_path);
    if (!parse_result) {
        au::log::error("Failed to parse existing repository database.");
        return {};
    }
    return *parse_result;
}

// Saves a list of packages to a repo.yaml file.
bool save_repo_db(const std::filesystem::path& repo_yaml_path, const std::vector<au::Package>& packages) {
    YAML::Node root_node; // A sequence
    for (const auto& pkg : packages) {
        root_node.push_back(package_to_yaml(pkg));
    }

    try {
        std::ofstream fout(repo_yaml_path);
        fout << root_node;
        return true;
    } catch (const std::exception& e) {
        au::log::error(std::string("Failed to write to repository database: ") + e.what());
        return false;
    }
}


// --- COMMAND IMPLEMENTATIONS ---

int cmd_init(const std::filesystem::path& repo_dir) {
    if (std::filesystem::exists(repo_dir / "repo.yaml")) {
        au::log::error("Repository already exists at: " + repo_dir.string());
        return 1;
    }
    std::filesystem::create_directories(repo_dir);
    if (save_repo_db(repo_dir / "repo.yaml", {})) {
        au::log::ok("Successfully initialized empty repository at: " + repo_dir.string());
        return 0;
    }
    return 1;
}

int cmd_add(const std::filesystem::path& repo_dir, const std::filesystem::path& package_file) {
    if (!std::filesystem::exists(package_file)) {
        au::log::error("Package file not found: " + package_file.string());
        return 1;
    }

    // 1. Extract metadata from the package to be added.
    auto meta_content = au::archive::extract_single_file_to_memory(package_file, ".AURORA_META");
    if (!meta_content) return 1;
    auto parse_result = au::Parser::parse_from_string(*meta_content);
    if (!parse_result) return 1;
    au::Package new_pkg = *parse_result;
    new_pkg.repo_name = repo_dir.filename().string(); // Tag with repo name

    // 2. Load the existing repository database.
    auto repo_yaml_path = repo_dir / "repo.yaml";
    auto packages = load_repo_db(repo_yaml_path);

    // 3. Remove any existing package with the same name.
    packages.erase(std::remove_if(packages.begin(), packages.end(),
                                  [&](const auto& p){ return p.name == new_pkg.name; }),
                   packages.end());

    // 4. Add the new package metadata.
    packages.push_back(new_pkg);

    // 5. Copy the package file into the repository.
    const std::string pkg_filename = new_pkg.name + "-" + new_pkg.version + ".pkg.tar.zst";
    try {
        std::filesystem::copy(package_file, repo_dir / pkg_filename, std::filesystem::copy_options::overwrite_existing);
    } catch (const std::exception& e) {
        au::log::error(std::string("Failed to copy package file into repository: ") + e.what());
        return 1;
    }

    // 6. Save the updated database.
    if (save_repo_db(repo_yaml_path, packages)) {
        au::log::ok("Successfully added '" + new_pkg.name + "' to the repository.");
        return 0;
    }
    return 1;
}

int cmd_remove(const std::filesystem::path& repo_dir, const std::string& package_name) {
    auto repo_yaml_path = repo_dir / "repo.yaml";
    auto packages = load_repo_db(repo_yaml_path);

    auto it = std::find_if(packages.begin(), packages.end(),
                           [&](const auto& p){ return p.name == package_name; });

    if (it == packages.end()) {
        au::log::error("Package '" + package_name + "' not found in the repository.");
        return 1;
    }

    const std::string pkg_filename = it->name + "-" + it->version + ".pkg.tar.zst";
    packages.erase(it);

    // Remove the package file.
    std::filesystem::remove(repo_dir / pkg_filename);

    if (save_repo_db(repo_yaml_path, packages)) {
        au::log::ok("Successfully removed '" + package_name + "' from the repository.");
        return 0;
    }
    return 1;
}


int main(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage();
        return 1;
    }

    std::string command = argv[1];
    std::filesystem::path repo_dir = argv[2];

    if (command == "init") {
        return cmd_init(repo_dir);
    } else if (command == "add") {
        if (argc < 4) { print_usage(); return 1; }
        return cmd_add(repo_dir, argv[3]);
    } else if (command == "remove") {
        if (argc < 4) { print_usage(); return 1; }
        return cmd_remove(repo_dir, argv[3]);
    } else {
        au::log::error("Unknown command: " + command);
        print_usage();
        return 1;
    }

    return 0;
}