//
// Created by cv2 on 8/15/25.
//

#pragma once

#include <filesystem>
#include <vector>
#include <expected>

namespace au {

    enum class ExtractError {
        OpenFile,
        ReadHeader,
        ExtractHeader,
        ExtractData,
        UnsupportedFormat,
        InternalError
    };

    std::expected<std::vector<std::filesystem::path>, ExtractError> extract(
            const std::filesystem::path& archive_path,
            const std::filesystem::path& destination_path
    );

    std::expected<std::string, ExtractError> extract_single_file_to_memory(
            const std::filesystem::path& archive_path,
            const std::filesystem::path& file_inside_archive
    );

} // namespace au::archive


