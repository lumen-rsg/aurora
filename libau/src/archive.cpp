#include "libau/archive.h"
#include "libau/logging.h"
#include <archive.h>
#include <archive_entry.h>
#include <vector>
#include <string>

namespace au {
    // Helper to ensure archive structs are always freed
    struct ArchiveReadGuard {
        archive* a;
        ArchiveReadGuard(archive* arch) : a(arch) {}
        ~ArchiveReadGuard() {
            if (a) {
                archive_read_free(a);
            }
        }
    };

    std::expected<std::vector<std::filesystem::path>, ExtractError> extract(
        const std::filesystem::path& archive_path,
        const std::filesystem::path& destination_path)
    {
        archive* a = archive_read_new();
        ArchiveReadGuard guard(a);
        archive_read_support_filter_all(a);
        archive_read_support_format_all(a);

        if (archive_read_open_filename(a, archive_path.c_str(), 10240) != ARCHIVE_OK) {
            log::error("libarchive could not open file: " + std::string(archive_error_string(a)));
            return std::unexpected(ExtractError::OpenFile);
        }

        std::filesystem::create_directories(destination_path);
        std::vector<std::filesystem::path> extracted_files;
        archive_entry* entry;

        while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
            const std::filesystem::path current_file = archive_entry_pathname(entry);

            // Security: Prevent path traversal ("zip slip") attacks.
            // We normalize the path and check if it still starts with the destination.
            const auto full_dest_path = (destination_path / current_file).lexically_normal();
            auto [root, rest] = std::mismatch(destination_path.begin(), destination_path.end(), full_dest_path.begin());
            if (root != destination_path.end()) {
                log::error("Archive contains malicious path: " + current_file.string());
                return std::unexpected(ExtractError::ExtractHeader);
            }

            archive_entry_set_pathname(entry, full_dest_path.c_str());

            if (archive_read_extract(a, entry, 0) != ARCHIVE_OK) {
                log::error("libarchive failed to extract: " + std::string(archive_error_string(a)));
                return std::unexpected(ExtractError::ExtractData);
            }

            // We only add regular files to our manifest list, not directories.
            if (AE_IFREG == archive_entry_filetype(entry)) {
                // We return the path relative to the destination, not the full system path.
                extracted_files.push_back(current_file);
            }
        }

        // Check for a final error after the loop
        if (archive_errno(a) != 0) {
            log::error("libarchive read error: " + std::string(archive_error_string(a)));
            return std::unexpected(ExtractError::ReadHeader);
        }

        return extracted_files;
    }


    std::expected<std::string, ExtractError> extract_single_file_to_memory(
            const std::filesystem::path& archive_path,
            const std::filesystem::path& file_inside_archive)
    {
        archive* a = archive_read_new();
        ArchiveReadGuard guard(a);
        archive_read_support_filter_all(a);
        archive_read_support_format_all(a);

        if (archive_read_open_filename(a, archive_path.c_str(), 10240) != ARCHIVE_OK) {
            log::error("libarchive could not open file: " + std::string(archive_error_string(a)));
            return std::unexpected(ExtractError::OpenFile);
        }

        archive_entry* entry;
        while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
            if (std::filesystem::path(archive_entry_pathname(entry)) == file_inside_archive) {
                la_int64_t size = archive_entry_size(entry);
                std::string content;
                content.resize(size);

                la_ssize_t read_bytes = archive_read_data(a, &content[0], content.size());
                if (read_bytes < 0) {
                    log::error("libarchive failed to read data: " + std::string(archive_error_string(a)));
                    return std::unexpected(ExtractError::ReadHeader);
                }
                if (static_cast<la_int64_t>(read_bytes) != size) {
                    log::error("Incomplete read of in-archive file.");
                    return std::unexpected(ExtractError::InternalError);
                }
                return content;
            }
        }

        log::error("File not found in archive: " + file_inside_archive.string());
        return std::unexpected(ExtractError::ReadHeader); // Closest error enum
    }
}