//
// Created by cv2 on 8/14/25.
//

#pragma once

#include "package.h"
#include <filesystem>
#include <expected>

namespace au {

    enum class ParseError {
        FileNotFound,
        InvalidFormat,
        MissingRequiredField
    };

    class Parser {
    public:
        // Parses a package definition file (YAML) and returns a Package object.
        // Uses std::expected for robust error handling (C++23).
        static std::expected<Package, ParseError> parse(const std::filesystem::path& file_path);

        static std::expected<std::vector<Package>, ParseError>
        parse_repository_index(const std::filesystem::path& file_path);

        static std::expected<Package, ParseError> parse_from_string(const std::string& content);
    };

} // namespace au