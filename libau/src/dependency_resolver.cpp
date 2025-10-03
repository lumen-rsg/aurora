//
// Created by cv2 on 8/14/25.
//

#include "libau/dependency_resolver.h"
#include "libau/logging.h" // For error messages

#include <algorithm> // For std::reverse

namespace au {

    DependencyResolver::DependencyResolver(const Database& db) : m_db(db) {}

    std::expected<ResolutionList, ResolveError> DependencyResolver::resolve(const std::vector<std::string>& package_names) {
        ResolutionList sorted_list; // This will hold the final, topologically sorted packages
        std::set<std::string> visiting; // Gray set: for detecting cycles
        std::set<std::string> visited;  // Black set: for nodes already processed

        for (const auto& pkg_name : package_names) {
            // Run DFS for each requested package if it hasn't been visited yet
            if (visited.find(pkg_name) == visited.end()) {
                auto result = dfs_visit(pkg_name, sorted_list, visiting, visited);
                if (!result) {
                    // If any dfs_visit fails, the whole resolution fails.
                    return std::unexpected(result.error());
                }
            }
        }

        return sorted_list;
    }


// The recursive DFS helper function.
    // Small helper to check if a string looks like a SONAME
bool is_soname(const std::string& name) {
    return name.find(".so") != std::string::npos;
}

struct ParsedDep {
    std::string name;
    std::string version;
};

ParsedDep parse_dependency_string(const std::string& dep_str) {
    auto pos = dep_str.find('=');
    if (pos != std::string::npos) {
        // It's a versioned dependency, e.g., "libncursesw.so=6-64"
        std::string name = dep_str.substr(0, pos);
        std::string version = dep_str.substr(pos + 1);
        // We only care about the major version, so strip suffixes like "-64"
        auto ver_pos = version.find('-');
        if (ver_pos != std::string::npos) {
            version = version.substr(0, ver_pos);
        }
        return {name, version};
    }
    return {dep_str, ""}; // No version specified
}


std::expected<void, ResolveError> DependencyResolver::dfs_visit(
    const std::string& dep_name,
    ResolutionList& sorted_list,
    std::set<std::string>& visiting,
    std::set<std::string>& visited)
{
    // --- STEP 1: Find the provider package for this dependency ---
    // This is now the FIRST thing we do.

    // A. Check if the dependency is satisfied by a package we've already processed.
    if (visited.count(dep_name)) {
        return {};
    }

    std::optional<Package> provider_pkg_opt;

    // B. Check if it's satisfied by a package that's already in our final install list.
    for (const auto& pkg : sorted_list) {
        if (pkg.name == dep_name) { return {}; } // The package itself is already planned
        for (const auto& provision : pkg.provides) {
            // A simple version check is sufficient here
            if (provision.rfind(dep_name, 0) == 0) { return {}; }
        }
    }

    // C. Check against already-installed packages on the system.
    for (const auto& installed_pkg : m_db.list_installed_packages()) {
        if (installed_pkg.name == dep_name) { return {}; }
        for (const auto& provision : installed_pkg.provides) {
            if (provision.rfind(dep_name, 0) == 0) { return {}; }
        }
    }

    // D. If not satisfied, find a provider in the repositories.
    provider_pkg_opt = m_db.find_repo_package(dep_name);
    if (!provider_pkg_opt) {
        log::progress("Searching for provider of '" + dep_name + "'...");
        for (const auto& repo_pkg : m_db.list_all_repo_packages()) {
            for (const auto& provision : repo_pkg.provides) {
                if (provision.rfind(dep_name, 0) == 0) {
                    provider_pkg_opt = repo_pkg;
                    goto found_provider;
                }
            }
        }
        found_provider:;
    }

    if (!provider_pkg_opt) {
        log::error("Could not satisfy dependency: '" + dep_name + "'");
        return std::unexpected(ResolveError::DependencyNotFound);
    }

    const Package& provider_pkg = *provider_pkg_opt;
    log::progress("Resolved dependency '" + dep_name + "' to provider package '" + provider_pkg.name + "'");

    // --- STEP 2: Perform Cycle Check and Recurse ---
    // We now operate ONLY on the provider package's name.

    // If we have already fully processed this provider, we're done.
    if (visited.count(provider_pkg.name)) {
        return {};
    }
    // If we are currently in the process of visiting this provider, we've found a cycle.
    if (visiting.count(provider_pkg.name)) {
        log::error("Circular dependency detected involving package: " + provider_pkg.name);
        return std::unexpected(ResolveError::CircularDependency);
    }

    // Mark the PROVIDER as being visited.
    visiting.insert(provider_pkg.name);

    // Recurse on the provider's dependencies.
    for (const auto& next_dep_name : provider_pkg.deps) {
        auto result = dfs_visit(next_dep_name, sorted_list, visiting, visited);
        if (!result) {
            // Propagate the error up the call stack.
            visiting.erase(provider_pkg.name);
            return result;
        }
    }

    // --- STEP 3: Finalize this Node ---
    // All dependencies for this provider are met. Move it from "visiting" to "visited".
    visiting.erase(provider_pkg.name);
    visited.insert(provider_pkg.name);

    // Add the provider package to our final, sorted list.
    sorted_list.push_back(provider_pkg);

    return {};
}
} // namespace au