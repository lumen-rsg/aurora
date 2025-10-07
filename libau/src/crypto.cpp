//
// Created by cv2 on 10/7/25.
//
#include "libau/logging.h"
#include "external/picosha2.h" // Your new header
#include <filesystem>
#include <fstream>
#include <expected>
#include <fstream>
#include <gpgme.h>
#include <iterator>

namespace au {

    bool verify_file_checksum(const std::filesystem::path& file_path, const std::string& expected_checksum) {
        if (!std::filesystem::exists(file_path)) {
            log::error("Cannot verify checksum: file does not exist at " + file_path.string());
            return false;
        }

        std::ifstream file(file_path, std::ios::binary);
        if (!file.is_open()) {
            log::error("Cannot verify checksum: failed to open file " + file_path.string());
            return false;
        }

        // --- CORRECTED HASHING LOGIC ---
        // Instead of passing the stream object directly, we pass iterators that
        // represent the beginning and end of the stream's content. This is unambiguous.
        std::string computed_hash = picosha2::hash256_hex_string(
            std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()
        );
        // --- END CORRECTION ---

        if (computed_hash != expected_checksum) {
            log::error("Checksum mismatch for: " + file_path.filename().string());
            log::error("  Expected: " + expected_checksum);
            log::error("  Computed: " + computed_hash);
            return false;
        }

        return true;
    }

    static void initialize_gpgme() {
    gpgme_check_version(nullptr);
    gpgme_engine_check_version(GPGME_PROTOCOL_OPENPGP);
}

bool verify_repository_signature(const std::filesystem::path& data_file, const std::filesystem::path& sig_file) {
    if (!std::filesystem::exists(data_file) || !std::filesystem::exists(sig_file)) {
        log::error("Cannot verify signature: data or signature file is missing.");
        return false;
    }

    initialize_gpgme();

    gpgme_ctx_t ctx;
    gpgme_error_t err = gpgme_new(&ctx);
    if (err) {
        log::error("GPGME context creation failed: " + std::string(gpgme_strerror(err)));
        return false;
    }

    // Set the trusted keyring path. GPGME can import all keys from a directory.
    const char* keydir = "/etc/aurora/keys/";
    err = gpgme_ctx_set_engine_info(ctx, GPGME_PROTOCOL_OPENPGP, nullptr, keydir);
    if (err) {
        log::error("GPGME failed to set key directory: " + std::string(gpgme_strerror(err)));
        gpgme_release(ctx);
        return false;
    }

    // GPGME works with "data" objects. We create them from our files.
    gpgme_data_t sig_data, text_data;
    gpgme_data_new_from_stream(&sig_data, fopen(sig_file.c_str(), "rb"));
    gpgme_data_new_from_stream(&text_data, fopen(data_file.c_str(), "rb"));

    // Perform the verification operation
    err = gpgme_op_verify(ctx, sig_data, text_data, nullptr);
    if (err) {
        log::error("GPGME verification failed: " + std::string(gpgme_strerror(err)));
        gpgme_data_release(sig_data);
        gpgme_data_release(text_data);
        gpgme_release(ctx);
        return false;
    }

    // Check the result
    gpgme_verify_result_t result = gpgme_op_verify_result(ctx);
    if (!result || !result->signatures) {
        log::error("Repository signature is not valid or no signature found.");
        gpgme_data_release(sig_data);
        gpgme_data_release(text_data);
        gpgme_release(ctx);
        return false;
    }

    // For a signature to be trusted, it must be both VALID and come from a key
    // that is in our trusted keyring (summary == GREEN).
    bool trusted_signature_found = false;
    for (gpgme_signature_t sig = result->signatures; sig; sig = sig->next) {
        if ((sig->summary & GPGME_SIGSUM_VALID) && (sig->summary & GPGME_SIGSUM_GREEN)) {
            log::info("Found valid, trusted signature from: " + std::string(sig->fpr));
            trusted_signature_found = true;
            break;
        }
    }

    // Cleanup
    gpgme_data_release(sig_data);
    gpgme_data_release(text_data);
    gpgme_release(ctx);

    if (!trusted_signature_found) {
        log::error("No valid signature from a trusted key could be found for the repository index.");
    }

    return trusted_signature_found;
}

} // namespace au::crypto