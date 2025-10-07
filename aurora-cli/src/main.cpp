#include <iostream>
#include <vector>
#include <string>
#include <unistd.h> // For geteuid()
#include <functional> // For std::function

// Our library and UI helpers
#include "ui_helpers.h"
#include <libau/package_manager.h>

void print_usage() {
    ui::error("Invalid usage.");
    std::cerr << "Usage: aurora [options] <command> [args...]\n\n"
              << "Options:\n"
              << "  --bootstrap <dir>   Operate on a different system root\n"
              << "  --force             Bypass safety checks\n"
              // REFINED: Add the new flag to the help text
              << "  --skip-crypto       Skip GPG and checksum verification (DANGEROUS)\n\n"
              << "Commands:\n"
              << "  sync                Synchronize package databases\n"
              << "  install <pkg>...    Install packages from repository\n"
              << "  install-local <file.au>...  Install local package files\n"
              << "  remove <pkg>...     Remove packages\n"
              << "  update              Update the entire system\n";
}

// REFINED: Updated error mapping
std::string error_to_string(au::TransactionError err) {
    switch (err) {
        case au::TransactionError::ResolutionFailed: return "Could not resolve dependencies.";
        // New, more specific resolver error
        case au::TransactionError::AmbiguousProvider: return "Dependency is provided by multiple packages; please be more specific.";
        case au::TransactionError::DownloadFailed: return "A package download failed.";
        case au::TransactionError::ChecksumMismatch: return "A package failed checksum verification.";
        case au::TransactionError::PackageAlreadyInstalled: return "One or more packages are already installed.";
        case au::TransactionError::PackageNotInstalled: return "One or more packages are not installed.";
        case au::TransactionError::FileConflict: return "A file conflict was detected.";
        case au::TransactionError::ExtractionFailed: return "Failed to extract a package archive.";
        case au::TransactionError::ScriptletFailed: return "A package scriptlet failed to execute.";
        case au::TransactionError::FileSystemError: return "A filesystem error occurred.";
        // New, more specific filesystem error
        case au::TransactionError::NotEnoughSpace: return "Not enough free disk space to complete the operation.";
        case au::TransactionError::DependencyViolation: return "A dependency violation was detected.";
        case au::TransactionError::ConflictDetected: return "A package conflict was detected.";
        default: return "An unknown transaction error occurred.";
    }
}

// REFINED: A generic helper to handle the Plan->Confirm->Execute pattern
int handle_transaction(
    const std::string& action_name,
    std::function<std::expected<au::Transaction, au::TransactionError>()> plan_func,
    std::function<std::expected<void, au::TransactionError>()> exec_func
) {
    auto plan_result = plan_func();
    if (!plan_result) {
        ui::error(error_to_string(plan_result.error()) + " (see details above)");
        return 1;
    }

    au::Transaction plan = std::move(*plan_result);
    if (plan.is_empty()) {
        ui::header("Nothing to do.");
        return 0;
    }

    ui::print_transaction_summary(plan);
    if (!ui::confirm("Proceed with " + action_name + "?")) {
        ui::warning(action_name + " aborted by user.");
        return 0;
    }

    auto exec_result = exec_func();
    if (exec_result) {
        ui::header(action_name + " completed successfully.");
        return 0;
    }

    ui::error(error_to_string(exec_result.error()) + " (see details above)");
    return 1;
}


// --- COMMAND HANDLERS (Now much simpler!) ---

int do_install_local(au::PackageManager& pm, const std::vector<std::string>& files, bool force) {
    ui::action("Installing local package files...");
    if (force) ui::warning("Forcing operation, safety checks are disabled!");

    for (const auto& file_path_str : files) {
        std::filesystem::path absolute_path = std::filesystem::absolute(file_path_str);

        if (!std::filesystem::exists(absolute_path)) {
            ui::error("File not found: " + absolute_path.string());
            return 1;
        }

        ui::header("Processing: " + absolute_path.string());
        auto result = pm.install_local_package(absolute_path, force);
        if (!result) {
            ui::error("Failed to install '" + absolute_path.string() + "': " + error_to_string(result.error()));
            return 1;
        }
    }

    ui::header("Local package installation completed successfully.");
    return 0;
}

int do_sync(au::PackageManager& pm) {
    ui::action("Synchronizing package databases...");
    if (pm.sync_database()) {
        ui::header("Synchronization complete.");
        return 0;
    }
    ui::error("Failed to synchronize databases.");
    return 1;
}

int do_update(au::PackageManager& pm, bool force) {
    ui::action("Starting system update...");
    if (force) ui::warning("Forcing operation, safety checks are disabled!");

    return handle_transaction(
        "update",
        [&]() { return pm.plan_update_transaction(force); },
        [&]() { return pm.update_system(force); }
    );
}

int do_install(au::PackageManager& pm, const std::vector<std::string>& packages, bool force) {
    ui::action("Resolving dependencies...");
    if (force) ui::warning("Forcing operation, safety checks are disabled!");

    return handle_transaction(
        "installation",
        [&]() { return pm.plan_install_transaction(packages, force); },
        [&]() { return pm.install(packages, force); }
    );
}

int do_remove(au::PackageManager& pm, const std::vector<std::string>& packages, bool force) {
    ui::action("Checking for reverse dependencies...");
    if (force) ui::warning("Forcing operation, safety checks are disabled!");

    return handle_transaction(
        "removal",
        [&]() { return pm.plan_remove_transaction(packages, force); },
        [&]() { return pm.remove(packages, force); }
    );
}

// --- Main Function with Advanced Argument Parsing ---

int main(int argc, char* argv[]) {
    if (geteuid() != 0) {
        ui::error("Aurora must be run as root to perform operations.");
        return 1;
    }

    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::vector<std::string> args(argv + 1, argv + argc);
    std::string bootstrap_root = "/";
    bool force_flag = false;
    bool skip_crypto_flag = false; // REFINED: New flag

    std::vector<std::string> main_args;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--bootstrap") {
            if (i + 1 >= args.size()) {
                ui::error("--bootstrap requires a directory argument.");
                return 1;
            }
            bootstrap_root = args[++i];
        } else if (arg == "--force") {
            force_flag = true;
        } else if (arg == "--skip-crypto") { // REFINED: Handle new flag
            skip_crypto_flag = true;
        }
        else {
            main_args.push_back(arg);
        }
    }

    if (main_args.empty()) {
        print_usage();
        return 1;
    }

    // REFINED: Pass all flags to the PackageManager constructor
    au::PackageManager pm(bootstrap_root, skip_crypto_flag);

    if (bootstrap_root != "/") {
        ui::header(std::string(ui::MAGENTA) + "Operating on bootstrap root: " + bootstrap_root);
    }

    const std::string& command = main_args[0];
    std::vector<std::string> packages;
    if (main_args.size() > 1) {
        packages.assign(main_args.begin() + 1, main_args.end());
    }

    // Command dispatch.
    if (command == "sync") {
        return do_sync(pm);
    } else if (command == "update") {
        return do_update(pm, force_flag);
    } else if (command == "install") {
        if (packages.empty()) { print_usage(); return 1; }
        return do_install(pm, packages, force_flag);
    } else if (command == "remove") {
        if (packages.empty()) { print_usage(); return 1; }
        return do_remove(pm, packages, force_flag);
    } else if (command == "install-local") {
        if (packages.empty()) { print_usage(); return 1; }
        return do_install_local(pm, packages, force_flag);
    } else {
        print_usage();
        return 1;
    }

    return 0;
}