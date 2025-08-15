//
// Created by cv2 on 8/16/25.
//

#pragma once

#include <package.h> // Include from libau
#include <yaml-cpp/yaml.h> // We need the yaml-cpp library

// Converts our C++ Package struct into a YAML Node for writing.
YAML::Node package_to_yaml(const au::Package& pkg) {
    YAML::Node node;
    node["name"] = pkg.name;
    node["version"] = pkg.version;
    node["arch"] = pkg.arch;

    // Always include the repo_name if it exists, as it's important for the resolver.
    if (!pkg.repo_name.empty()) {
        node["repo_name"] = pkg.repo_name;
    }
    if (!pkg.description.empty()) {
        node["description"] = pkg.description;
    }

    // --- Helper to add a sequence of strings if it's not empty ---
    auto add_string_seq = [&](const std::string& key, const std::vector<std::string>& vec){
        if (!vec.empty()) {
            for(const auto& item : vec) {
                node[key].push_back(item);
            }
        }
    };

    // --- Helper to add a sequence of paths if it's not empty ---
    auto add_path_seq = [&](const std::string& key, const std::vector<std::filesystem::path>& vec){
        if (!vec.empty()) {
            for(const auto& item : vec) {
                node[key].push_back(item.string());
            }
        }
    };

    // --- Add all sequence fields ---
    add_string_seq("deps", pkg.deps);
    add_string_seq("makedepends", pkg.makedepends);
    add_string_seq("conflicts", pkg.conflicts);
    add_string_seq("replaces", pkg.replaces);
    add_string_seq("provides", pkg.provides);
    add_path_seq("files", pkg.files);

    // --- Add all optional script fields ---
    if (!pkg.pre_install_script.empty()) {
        node["pre_install"] = pkg.pre_install_script;
    }
    if (!pkg.post_install_script.empty()) {
        node["post_install"] = pkg.post_install_script;
    }
    if (!pkg.pre_remove_script.empty()) {
        node["pre_remove"] = pkg.pre_remove_script;
    }
    if (!pkg.post_remove_script.empty()) {
        node["post_remove"] = pkg.post_remove_script;
    }

    return node;
}