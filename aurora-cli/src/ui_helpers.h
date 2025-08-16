//
// Created by cv2 on 8/16/25.
//

#pragma once

#include <iostream>
#include <string>
#include <vector>
#include "../../libau/include/package_manager.h"
#include "../../libau/include/package.h"

namespace ui {

// --- ANSI Color Codes ---
    const char* const RESET = "\033[0m";
    const char* const BOLD = "\033[1m";
    const char* const BLUE = "\033[1;34m";
    const char* const GREEN = "\033[0;32m";
    const char* const RED = "\033[1;31m";
    const char* const YELLOW = "\033[1;33m";
    const char* const CYAN = "\033[0;36m";

// --- Formatted Printing Functions ---

    void action(const std::string& msg) {
        std::cout << BLUE << ":: " << RESET << BOLD << msg << RESET << std::endl;
    }

    void header(const std::string& msg) {
        std::cout << BOLD << msg << RESET << std::endl;
    }

    void item(const std::string& msg) {
        std::cout << " " << GREEN << "-" << RESET << " " << msg << std::endl;
    }

    void error(const std::string& msg) {
        std::cerr << RED << "error: " << RESET << msg << std::endl;
    }

    void warning(const std::string& msg) {
        std::cout << YELLOW << "warning: " << RESET << msg << std::endl;
    }

// Asks the user a "Yes/No" question.
    bool confirm(const std::string& question) {
        std::cout << CYAN << ":: " << RESET << BOLD << question << " [Y/n] " << RESET;
        std::string response;
        std::getline(std::cin, response);
        return response.empty() || response[0] == 'y' || response[0] == 'Y';
    }

    // Prints a formatted list of packages from a transaction plan.
    void print_transaction_summary(const au::Transaction& plan) {
        if (!plan.to_remove.empty()) {
            header("\nPackages to remove:");
            for (const auto& pkg : plan.to_remove) { // Use a non-conflicting name
                item(pkg.name + " " + pkg.version);
            }
        }
        if (!plan.to_install.empty()) {
            header("\nPackages to install:");
            for (const auto& install_item : plan.to_install) {
                item(install_item.metadata.name + " " + install_item.metadata.version);
            }
        }
        std::cout << std::endl;
    }

} // namespace ui