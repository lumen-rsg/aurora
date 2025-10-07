//
// Created by cv2 on 8/14/25.
//

#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace au {

    struct Package {
        std::string name;
        std::string version;
        std::string arch;
        std::string description;

        // --- Dependencies ---
        std::vector<std::string> deps;
        std::vector<std::string> makedepends;

        // --- Relationships ---
        std::vector<std::string> conflicts;
        std::vector<std::string> replaces;
        std::vector<std::string> provides; // NEW: Virtual provisions, e.g., "openssl-1.1"

        // --- Scripts (optional paths within the package) ---
        std::string pre_install_script;
        std::string post_install_script;
        std::string pre_remove_script;
        std::string post_remove_script;
        std::string repo_name;
        std::string checksum;

        // --- File Manifest ---
        std::vector<std::filesystem::path> files; // NEW: The list of files this package contains.
    };

    // Represents a package that is installed on the system.
    struct InstalledPackage : public Package {
        std::filesystem::path install_path;
        std::string install_date;
        // A list of files owned by this package
        std::vector<std::filesystem::path> owned_files;
    };

} // namespace au