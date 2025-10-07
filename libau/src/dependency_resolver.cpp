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
    // --- STEP 1: Check if this dependency is already satisfied ---
    // (This part of the logic remains the same)
    if (visited.count(dep_name)) { return {}; }
    for (const auto& pkg : sorted_list) {
        if (pkg.name == dep_name) { return {}; }
        for (const auto& provision : pkg.provides) {
            if (provision == dep_name) { return {}; }
        }
    }
    for (const auto& installed_pkg : m_db.list_installed_packages()) {
        if (installed_pkg.name == dep_name) { return {}; }
        for (const auto& provision : installed_pkg.provides) {
            if (provision == dep_name) { return {}; }
        }
    }

    // --- STEP 2: Find ALL potential providers for this dependency ---
    std::optional<Package> real_package_provider;
    std::vector<Package> virtual_providers;

    // We now search the entire repository list to gather all candidates.
    for (const auto& repo_pkg : m_db.list_all_repo_packages()) {
        // Rule 1: Does this package have the exact name? This is our highest priority.
        if (repo_pkg.name == dep_name) {
            real_package_provider = repo_pkg;
            continue; // Keep searching for virtual providers for ambiguity check
        }
        // Rule 2: Does this package provide the dependency?
        for (const auto& provision : repo_pkg.provides) {
            if (provision == dep_name) {
                virtual_providers.push_back(repo_pkg);
                break;
            }
        }
    }

    // --- STEP 3: Select the BEST provider based on our rules ---
    std::optional<Package> provider_pkg_opt;
    if (real_package_provider) {
        // We found a package with the exact name. This is our preferred choice.
        provider_pkg_opt = real_package_provider;
        log::progress("Resolved dependency '" + dep_name + "' to real package '" + provider_pkg_opt->name + "'");
    } else if (virtual_providers.size() == 1) {
        // No real package, but exactly one virtual provider. This is also a clear choice.
        provider_pkg_opt = virtual_providers.front();
        log::progress("Resolved dependency '" + dep_name + "' to virtual provider '" + provider_pkg_opt->name + "'");
    } else if (virtual_providers.empty()) {
        // No real package and no virtual providers found.
        log::error("Could not satisfy dependency: '" + dep_name + "'. No package found.");
        return std::unexpected(ResolveError::DependencyNotFound);
    } else {
        // No real package, but MULTIPLE virtual providers. This is an ambiguity error.
        std::string provider_list;
        for(const auto& p : virtual_providers) { provider_list += p.name + " "; }
        log::error("Ambiguous dependency: '" + dep_name + "' is provided by multiple packages: " + provider_list);
        log::error("Please install one of the providers explicitly.");
        return std::unexpected(ResolveError::AmbiguousProvider);
    }

    // --- STEP 4: Perform Cycle Check and Recurse (using the selected provider) ---
    // (The rest of the function proceeds as before, using the now-unambiguous provider_pkg)
    const Package& provider_pkg = *provider_pkg_opt;

    if (visited.count(provider_pkg.name)) { return {}; }
    if (visiting.count(provider_pkg.name)) {
        log::error("Circular dependency detected involving package: " + provider_pkg.name);
        return std::unexpected(ResolveError::CircularDependency);
    }

    visiting.insert(provider_pkg.name);
    for (const auto& next_dep_name : provider_pkg.deps) {
        auto result = dfs_visit(next_dep_name, sorted_list, visiting, visited);
        if (!result) {
            visiting.erase(provider_pkg.name);
            return result;
        }
    }

    visiting.erase(provider_pkg.name);
    visited.insert(provider_pkg.name);
    sorted_list.push_back(provider_pkg);

    return {};
}
} // namespace au