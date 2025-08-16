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
    std::expected<void, ResolveError> DependencyResolver::dfs_visit(
            const std::string& package_name,
            ResolutionList& sorted_list,
            std::set<std::string>& visiting,
            std::set<std::string>& visited)
    {
        // If the package is already installed, we don't need to do anything.
        if (m_db.is_package_installed(package_name)) {
            return {}; // Successful no-op
        }

        // Mark current node as being visited (add to gray set).
        visiting.insert(package_name);

        // Find the package in our repository database.
        auto pkg_opt = m_db.find_repo_package(package_name);
        if (!pkg_opt) {
            log::error("Dependency '" + package_name + "' not found in any repository.");
            return std::unexpected(ResolveError::DependencyNotFound);
        }
        const Package& current_pkg = *pkg_opt;

        // Recurse for all dependencies of the current package.
        for (const auto& dep_name : current_pkg.deps) {
            // If we encounter a node that is currently in the 'visiting' set, we have found a cycle.
            if (visiting.count(dep_name)) {
                log::error("Circular dependency detected: " + package_name + " -> " + dep_name);
                return std::unexpected(ResolveError::CircularDependency);
            }

            // If the dependency hasn't been fully processed yet, visit it.
            if (visited.find(dep_name) == visited.end()) {
                auto result = dfs_visit(dep_name, sorted_list, visiting, visited);
                if (!result) {
                    return result; // Propagate the error up
                }
            }
        }

        // All dependencies of this node have been visited.
        // Remove from the gray set and add to the black set.
        visiting.erase(package_name);
        visited.insert(package_name);

        // Add the current package to our sorted list.
        sorted_list.push_back(current_pkg);

        return {}; // Success
    }

} // namespace au