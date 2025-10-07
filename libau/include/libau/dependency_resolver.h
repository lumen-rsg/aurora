//
// Created by cv2 on 8/14/25.
//

#pragma once

#include "package.h"
#include "database.h"
// #include "repository.h" // We can remove this for now to clean up includes

#include <vector>
#include <string>
#include <expected>
#include <set>

namespace au {

    enum class ResolveError {
        PackageNotFound,
        DependencyNotFound,
        CircularDependency,
        ConflictDetected,
        AmbiguousProvider
    };

    using ResolutionList = std::vector<Package>;

    class DependencyResolver {
    public:
        explicit DependencyResolver(const Database& db);

        std::expected<ResolutionList, ResolveError> resolve(const std::vector<std::string>& package_names);

    private:
        std::expected<void, ResolveError> dfs_visit(
                const std::string& package_name,
                std::vector<Package>& sorted_list,
                std::set<std::string>& visiting,
                std::set<std::string>& visited
        );

        const Database& m_db;
    };

} // namespace au