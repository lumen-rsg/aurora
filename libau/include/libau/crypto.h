//
// Created by cv2 on 10/7/25.
//

#ifndef AURORA_CRYPTO_H
#define AURORA_CRYPTO_H
#include <filesystem>

namespace au {
    bool verify_file_checksum(const std::filesystem::path& file_path, const std::string& expected_checksum);
    bool verify_repository_signature(const std::filesystem::path& data_file, const std::filesystem::path& sig_file);
}

#endif //AURORA_CRYPTO_H