//
// Created by cv2 on 8/14/25.
//

#include "libau/parser.h"
#include "libau/logging.h"

#include <yaml-cpp/yaml.h>
#include <fstream>

namespace au {

    static std::string get_optional_scalar(const YAML::Node& node, const std::string& key) {
        if (node[key] && node[key].IsScalar()) {
            return node[key].as<std::string>();
        }
        return "";
    }

    template<typename T>
    static std::expected<T, ParseError> get_required_scalar(const YAML::Node& node, const std::string& key) {
        if (!node[key]) {
            au::log::error("Missing required field: '" + key + "'");
            return std::unexpected(ParseError::MissingRequiredField);
        }
        return node[key].as<T>();
    }

    static std::vector<std::string> get_optional_sequence(const YAML::Node& node, const std::string& key) {
        std::vector<std::string> result;
        if (node[key] && node[key].IsSequence()) {
            for (const auto& item : node[key]) {
                result.push_back(item.as<std::string>());
            }
        }
        return result;
    }

    static std::expected<Package, ParseError> parse_package_node(const YAML::Node& node) {
        Package pkg;

        auto name_res = get_required_scalar<std::string>(node, "name");
        if (!name_res) return std::unexpected(name_res.error());
        pkg.name = *name_res;

        auto version_res = get_required_scalar<std::string>(node, "version");
        if (!version_res) return std::unexpected(version_res.error());
        pkg.version = *version_res;

        auto arch_res = get_required_scalar<std::string>(node, "arch");
        if (!arch_res) return std::unexpected(arch_res.error());
        pkg.arch = *arch_res;

        if (node["description"]) {
            pkg.description = node["description"].as<std::string>();
        }

        if (node["installed_size"] && node["installed_size"].IsScalar()) {
            pkg.installed_size = node["installed_size"].as<int64_t>();
        }

        pkg.deps = get_optional_sequence(node, "deps");
        pkg.makedepends = get_optional_sequence(node, "makedepends");

        pkg.conflicts = get_optional_sequence(node, "conflicts");
        pkg.replaces = get_optional_sequence(node, "replaces");

        pkg.provides = get_optional_sequence(node, "provides");
        pkg.pre_install_script = get_optional_scalar(node, "pre_install");
        pkg.post_install_script = get_optional_scalar(node, "post_install");
        pkg.pre_remove_script = get_optional_scalar(node, "pre_remove");
        pkg.post_remove_script = get_optional_scalar(node, "post_remove");

        auto checksum_res = get_required_scalar<std::string>(node, "checksum");
        if (!checksum_res) return std::unexpected(checksum_res.error());
        pkg.checksum = *checksum_res;

        if (node["files"] && node["files"].IsSequence()) {
            for (const auto& item : node["files"]) {
                pkg.files.emplace_back(item.as<std::string>());
            }
        }

        return pkg;
    }

    std::expected<Package, ParseError> Parser::parse(const std::filesystem::path& file_path) {
        if (!std::filesystem::exists(file_path)) {
            return std::unexpected(ParseError::FileNotFound);
        }
        try {
            YAML::Node root = YAML::LoadFile(file_path.string());
            return parse_package_node(root);
        } catch (const YAML::Exception& e) {
            log::error("Failed to parse YAML file " + file_path.string() + ": " + e.what());
            return std::unexpected(ParseError::InvalidFormat);
        }
    }

    std::expected<std::vector<Package>, ParseError> Parser::parse_repository_index(const std::filesystem::path& file_path) {
        if (!std::filesystem::exists(file_path)) {
            return std::unexpected(ParseError::FileNotFound);
        }

        YAML::Node root;
        try {
            root = YAML::LoadFile(file_path.string());
        } catch (const YAML::Exception& e) {
            log::error("Failed to parse repo index " + file_path.string() + ": " + e.what());
            return std::unexpected(ParseError::InvalidFormat);
        }

        if (!root.IsSequence()) {
            log::error("Repository index is not a valid YAML sequence: " + file_path.string());
            return std::unexpected(ParseError::InvalidFormat);
        }

        std::vector<Package> packages;
        for (const auto& node : root) {
            auto pkg_result = parse_package_node(node);
            if (pkg_result) {
                packages.push_back(*pkg_result);
            } else {
                log::error("Skipping invalid package definition in repo index.");
                // For a repo index, we might just skip bad entries instead of failing entirely
            }
        }
        return packages;
    }

    std::expected<Package, ParseError> Parser::parse_from_string(const std::string& content) {
        try {
            YAML::Node root = YAML::Load(content);
            return parse_package_node(root);
        } catch (const YAML::Exception& e) {
            log::error(std::string("Failed to parse YAML from string: ") + e.what());
            return std::unexpected(ParseError::InvalidFormat);
        }
    }

} // namespace au