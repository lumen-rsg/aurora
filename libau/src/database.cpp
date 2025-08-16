//
// Created by cv2 on 8/14/25.
//

#include "libau/database.h"
#include "libau/logging.h"
#include <sstream> // For string joining/splitting

// This is a big header, so we include it only in the .cpp file.
#define SQLITE_ORM_OMITS_CODECVT // Avoids deprecation warnings with C++17 and later
#include "external/sqlite_orm.h"

namespace au {

    std::string join(const std::vector<std::filesystem::path>& vec, const char* delim = "\n") {
        std::stringstream ss;
        for (size_t i = 0; i < vec.size(); ++i) {
            // Use .generic_string() for a consistent, cross-platform path representation
            ss << vec[i].generic_string();
            if (i < vec.size() - 1) {
                ss << delim;
            }
        }
        return ss.str();
    }

    std::vector<std::filesystem::path> split_paths(const std::string& str, char delim = '\n') {
        if (str.empty()) {
            return {};
        }
        std::vector<std::filesystem::path> paths;
        std::string token;
        std::istringstream tokenStream(str);
        while (std::getline(tokenStream, token, delim)) {
            if (!token.empty()) { // Don't create empty paths
                paths.emplace_back(token);
            }
        }
        return paths;
    }

// --- Helper functions for serializing vector<string> ---
    std::string join(const std::vector<std::string>& vec, const char* delim = "\n") {
        std::stringstream ss;
        for (size_t i = 0; i < vec.size(); ++i) {
            ss << vec[i];
            if (i < vec.size() - 1) {
                ss << delim;
            }
        }
        return ss.str();
    }

    std::vector<std::string> split(const std::string& str, char delim = '\n') {
        if (str.empty()) {
            return {};
        }
        std::vector<std::string> tokens;
        std::string token;
        std::istringstream tokenStream(str);
        while (std::getline(tokenStream, token, delim)) {
            tokens.push_back(token);
        }
        return tokens;
    }

// --- ORM Table Definitions ---
// These structs represent the DB schema.
    namespace db_schema {
        struct InstalledPackage {
            std::string name;
            std::string version;
            std::string arch;
            std::string description;
            std::string deps; // Serialized
            std::string makedepends; // Serialized
            std::string conflicts;
            std::string replaces;
            std::string provides;
            std::string install_date;
            std::string owned_files; // Serialized
            std::string pre_install_script;
            std::string post_install_script;
            std::string pre_remove_script;
            std::string post_remove_script;
            std::string repo_name;
        };

        struct RepoPackage {
            std::string name;
            std::string version;
            std::string arch;
            std::string description;
            std::string deps; // Serialized
            std::string makedepends; // Serialized
            std::string conflicts; // Serialized
            std::string replaces; // Serialized
            std::string provides;
            std::string files;
            std::string pre_install_script;
            std::string post_install_script;
            std::string pre_remove_script;
            std::string post_remove_script;
            std::string repo_name;
        };
    }

// --- ORM Storage Factory ---
// This creates the storage object and defines the table schemas for sqlite_orm.
    auto create_storage(const std::filesystem::path& db_path) {
        using namespace sqlite_orm;
        return make_storage(db_path.string(),
            make_table("installed_packages",
               make_column("name", &db_schema::InstalledPackage::name, primary_key()),
               make_column("version", &db_schema::InstalledPackage::version),
               make_column("arch", &db_schema::InstalledPackage::arch),
               make_column("description", &db_schema::InstalledPackage::description),
               make_column("deps", &db_schema::InstalledPackage::deps),
               make_column("makedepends", &db_schema::InstalledPackage::makedepends),
               make_column("conflicts", &db_schema::InstalledPackage::conflicts),
               make_column("replaces", &db_schema::InstalledPackage::replaces),
               make_column("provides", &db_schema::InstalledPackage::provides),
               make_column("owned_files", &db_schema::InstalledPackage::owned_files),
               make_column("install_date", &db_schema::InstalledPackage::install_date),
               make_column("pre_install_script", &db_schema::InstalledPackage::pre_install_script),
               make_column("post_install_script", &db_schema::InstalledPackage::post_install_script),
               make_column("pre_remove_script", &db_schema::InstalledPackage::pre_remove_script),
               make_column("repo_name", &db_schema::InstalledPackage::pre_remove_script), // NEW
               make_column("post_remove_script", &db_schema::InstalledPackage::post_remove_script)
            ),
            make_table("repo_packages",
               make_column("name", &db_schema::RepoPackage::name, primary_key()),
               make_column("version", &db_schema::RepoPackage::version),
               make_column("arch", &db_schema::RepoPackage::arch),
               make_column("description", &db_schema::RepoPackage::description),
               make_column("deps", &db_schema::RepoPackage::deps),
               make_column("makedepends", &db_schema::RepoPackage::makedepends),
               make_column("conflicts", &db_schema::RepoPackage::conflicts),
               make_column("replaces", &db_schema::RepoPackage::replaces),
               make_column("provides", &db_schema::RepoPackage::provides),
               make_column("files", &db_schema::RepoPackage::files),
               make_column("pre_install_script", &db_schema::RepoPackage::pre_install_script),
               make_column("post_install_script", &db_schema::RepoPackage::post_install_script),
               make_column("pre_remove_script", &db_schema::RepoPackage::pre_remove_script),
               make_column("repo_name", &db_schema::RepoPackage::repo_name), // NEW
               make_column("post_remove_script", &db_schema::RepoPackage::post_remove_script)
            )
        );
    }

// --- The PIMPL Implementation ---
    struct Database::Impl {
        using Storage = decltype(create_storage(""));
        Storage storage;

        explicit Impl(const std::filesystem::path& db_path) : storage(create_storage(db_path)) {
            storage.sync_schema(true); // `true` adds columns if they're missing
        }
    };

// --- Conversion Functions ---
    db_schema::InstalledPackage to_db(const InstalledPackage& pkg) {
        return {
                pkg.name, pkg.version, pkg.arch, pkg.description,
                join(pkg.deps), join(pkg.makedepends), join(pkg.conflicts), join(pkg.replaces),
                join(pkg.provides), // NEW
                pkg.install_date,
                join(pkg.owned_files),
                pkg.pre_install_script,
                pkg.post_install_script,
                pkg.pre_remove_script,
                pkg.post_remove_script,
                pkg.repo_name
        };
    }

    InstalledPackage from_db(const db_schema::InstalledPackage& db_pkg) {
        InstalledPackage pkg;
        pkg.name = db_pkg.name;
        pkg.version = db_pkg.version;
        pkg.arch = db_pkg.arch;
        pkg.description = db_pkg.description;
        pkg.deps = split(db_pkg.deps);
        pkg.makedepends = split(db_pkg.makedepends);
        pkg.conflicts = split(db_pkg.conflicts);
        pkg.replaces = split(db_pkg.replaces);
        pkg.install_date = db_pkg.install_date;
        pkg.provides = split(db_pkg.provides); // NEW
        pkg.owned_files = split_paths(db_pkg.owned_files); // We'll need a split_paths helper
        pkg.pre_install_script = db_pkg.pre_install_script;
        pkg.post_install_script = db_pkg.post_install_script;
        pkg.pre_remove_script = db_pkg.pre_remove_script;
        pkg.post_remove_script = db_pkg.post_remove_script;
        pkg.repo_name = db_pkg.repo_name;
        return pkg;
    }

    db_schema::RepoPackage to_db(const Package& pkg) {
        return {
                pkg.name, pkg.version, pkg.arch, pkg.description,
                join(pkg.deps), join(pkg.makedepends), join(pkg.conflicts), join(pkg.replaces),
                join(pkg.provides), join(pkg.files),
                pkg.pre_install_script, pkg.post_install_script,
                pkg.pre_remove_script, pkg.post_remove_script,
                pkg.repo_name
        };
    }

    Package from_db(const db_schema::RepoPackage& db_pkg) {
        Package pkg;
        pkg.name = db_pkg.name;
        pkg.version = db_pkg.version;
        pkg.arch = db_pkg.arch;
        pkg.description = db_pkg.description;
        pkg.deps = split(db_pkg.deps);
        pkg.makedepends = split(db_pkg.makedepends);
        pkg.conflicts = split(db_pkg.conflicts);
        pkg.replaces = split(db_pkg.replaces);
        pkg.provides = split(db_pkg.provides);
        pkg.files = split_paths(db_pkg.files); // NEW
        pkg.pre_install_script = db_pkg.pre_install_script;
        pkg.post_install_script = db_pkg.post_install_script;
        pkg.pre_remove_script = db_pkg.pre_remove_script;
        pkg.post_remove_script = db_pkg.post_remove_script;
        pkg.repo_name = db_pkg.repo_name;
        return pkg;
    }


// --- Public Method Implementations ---

    Database::Database(const std::filesystem::path& db_path)
            : pimpl(std::make_unique<Impl>(db_path)) {}

    Database::~Database() = default; // Important for unique_ptr with incomplete type

    void Database::add_installed_package(const InstalledPackage& pkg) {
        try {
            pimpl->storage.replace(to_db(pkg));
        } catch (const std::exception& e) {
            log::error(std::string("Failed to add installed package '") + pkg.name + "': " + e.what());
        }
    }

    void Database::remove_installed_package(const std::string& name) {
        try {
            pimpl->storage.remove<db_schema::InstalledPackage>(name);
        } catch (const std::exception& e) {
            log::error(std::string("Failed to remove installed package '") + name + "': " + e.what());
        }
    }

    std::optional<InstalledPackage> Database::get_installed_package(const std::string& name) const {
        auto db_pkg_ptr = pimpl->storage.get_pointer<db_schema::InstalledPackage>(name);
        if (db_pkg_ptr) {
            return from_db(*db_pkg_ptr);
        }
        return std::nullopt;
    }

    bool Database::is_package_installed(const std::string& name) const {
        return get_installed_package(name).has_value();
    }

    std::vector<InstalledPackage> Database::list_installed_packages() const {
        auto all_db_pkgs = pimpl->storage.get_all<db_schema::InstalledPackage>();
        std::vector<InstalledPackage> result;
        result.reserve(all_db_pkgs.size());
        for (const auto& db_pkg : all_db_pkgs) {
            result.push_back(from_db(db_pkg));
        }
        return result;
    }

    void Database::sync_repo_packages(const std::vector<Package>& packages) {
        try {
            // sqlite_orm uses a lambda for transactions.
            // It returns true on commit, false on rollback.
            bool success = pimpl->storage.transaction([&] {
                // This code block is now executed within a single database transaction.
                pimpl->storage.remove_all<db_schema::RepoPackage>();

                for (const auto& pkg : packages) {
                    pimpl->storage.replace(to_db(pkg));
                }

                return true; // Return true to commit the transaction.
            });

            if (success) {
                log::info("Synced " + std::to_string(packages.size()) + " packages to repository database.");
            } else {
                // This would happen if the lambda returned false.
                log::error("Repository sync transaction was rolled back by request.");
            }

        } catch (const std::exception& e) {
            // This will catch exceptions from within the lambda, after the rollback has occurred.
            log::error(std::string("Failed to sync repo packages (transaction rolled back): ") + e.what());
        }
    }

    std::optional<Package> Database::find_repo_package(const std::string& name) const {
        auto db_pkg_ptr = pimpl->storage.get_pointer<db_schema::RepoPackage>(name);
        if (db_pkg_ptr) {
            return from_db(*db_pkg_ptr);
        }
        return std::nullopt;
    }

} // namespace au