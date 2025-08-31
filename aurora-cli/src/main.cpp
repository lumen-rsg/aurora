//
// Created by cv2 on 8/16/25.
//

#include <iostream>
#include <vector>
#include <string>
#include <unistd.h> // For geteuid()

// Our library and UI helpers
#include "ui_helpers.h"
#include <libau/package.h>

void print_usage() {
    ui::error("Invalid usage.");
    std::cerr << "Usage:\n"
              << "  aurora sync                  Synchronize package databases\n"
              << "  aurora install <pkg>...      Install packages\n"
              << "  aurora remove <pkg>...       Remove packages\n"
              << "  aurora update                Update the entire system\n";
}

// Translates a TransactionError into a user-friendly string.
std::string error_to_string(au::TransactionError err) {
    switch (err) {
        case au::TransactionError::ResolutionFailed: return "Could not resolve dependencies.";
        case au::TransactionError::DownloadFailed: return "A package download failed.";
        case au::TransactionError::ChecksumMismatch: return "A package failed checksum verification.";
        case au::TransactionError::PackageAlreadyInstalled: return "One or more packages are already installed.";
        case au::TransactionError::PackageNotInstalled: return "One or more packages are not installed.";
        case au::TransactionError::FileConflict: return "A file conflict was detected.";
        case au::TransactionError::ExtractionFailed: return "Failed to extract a package archive.";
        case au::TransactionError::ScriptletFailed: return "A package scriptlet failed to execute.";
        case au::TransactionError::FileSystemError: return "A filesystem error occurred.";
        case au::TransactionError::DependencyViolation: return "A dependency violation was detected.";
        default: return "An unknown transaction error occurred.";
    }
}

// --- COMMAND HANDLERS ---

int do_sync(au::PackageManager& pm) {
    ui::action("Synchronizing package databases...");
    if (pm.sync_database()) {
        ui::header("Synchronization complete.");
        return 0;
    }
    ui::error("Failed to synchronize databases.");
    return 1;
}

int do_update(au::PackageManager& pm) {
    ui::action("Starting system update...");
    auto plan_result = pm.plan_update_transaction();
    if (!plan_result) {
        ui::error(error_to_string(plan_result.error()));
        return 1;
    }
    au::Transaction plan = std::move(*plan_result);
    if (plan.is_empty()) {
        ui::header("System is already up to date.");
        return 0;
    }
    ui::print_transaction_summary(plan);
    if (!ui::confirm("Proceed with update?")) return 0;

    auto update_result = pm.update_system();
    if (update_result) {
        ui::header("System update completed successfully.");
        return 0;
    }
    ui::error(error_to_string(update_result.error()));
    return 1;
}

int do_install(au::PackageManager& pm, const std::vector<std::string>& packages) {
    ui::action("Resolving dependencies...");
    auto plan_result = pm.plan_install_transaction(packages);
    if (!plan_result) {
        ui::error(error_to_string(plan_result.error()));
        return 1;
    }
    au::Transaction plan = std::move(*plan_result);
    if (plan.is_empty()) {
        ui::warning("Nothing to do.");
        return 0;
    }
    ui::print_transaction_summary(plan);
    if (!ui::confirm("Proceed with installation?")) return 0;

    auto install_result = pm.install(packages);
    if (install_result) {
        ui::header("Installation completed successfully.");
        return 0;
    }
    ui::error(error_to_string(install_result.error()));
    return 1;
}

int do_remove(au::PackageManager& pm, const std::vector<std::string>& packages) {
    ui::action("Checking for reverse dependencies...");
    auto plan_result = pm.plan_remove_transaction(packages);
    if (!plan_result) {
        ui::error(error_to_string(plan_result.error()));
        return 1;
    }
    au::Transaction plan = std::move(*plan_result);
    if (plan.is_empty()) {
        ui::warning("Nothing to do.");
        return 0;
    }
    ui::print_transaction_summary(plan);
    if (!ui::confirm("Proceed with removal?")) return 0;

    auto remove_result = pm.remove(packages);
    if (remove_result) {
        ui::header("Removal completed successfully.");
        return 0;
    }
    ui::error(error_to_string(remove_result.error()));
    return 1;
}


int main(int argc, char* argv[]) {
    if (geteuid() != 0) {
        ui::error("Aurora must be run as root to perform operations.");
        return 1;
    }

    if (argc < 2) {
        print_usage();
        return 1;
    }

    // Initialize our backend package manager for the live system ("/")
    au::PackageManager pm("/");

    std::string command = argv[1];
    std::vector<std::string> packages;
    for (int i = 2; i < argc; ++i) {
        packages.push_back(argv[i]);
    }

    if (command == "sync") {
        return do_sync(pm);
    } else if (command == "update") {
        return do_update(pm);
    } else if (command == "install") {
        if (packages.empty()) { print_usage(); return 1; }
        return do_install(pm, packages);
    } else if (command == "remove") {
        if (packages.empty()) { print_usage(); return 1; }
        return do_remove(pm, packages);
    } else {
        print_usage();
        return 1;
    }

    return 0;
}