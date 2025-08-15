#include "archive.h"
#include "logging.h"
#include <cstdio>
#include <memory>
#include <array>
#include <stdexcept>
#include <sys/wait.h>

namespace au::archive {

    // RAII helper to safely manage changing the current working directory.
    class CWDGuard {
    public:
        CWDGuard() : m_old_path(std::filesystem::current_path()) {}
        ~CWDGuard() {
            std::error_code ec;
            std::filesystem::current_path(m_old_path, ec);
        }
    private:
        std::filesystem::path m_old_path;
    };

// Helper to execute a command and capture its stdout.
// Throws a runtime_error on failure.
    static std::string exec_and_capture(const char* cmd) {
        std::array<char, 128> buffer;
        std::string result;
        // Use a unique_ptr for RAII-style cleanup of the FILE* handle from popen
        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
        if (!pipe) {
            throw std::runtime_error("popen() failed!");
        }
        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            result += buffer.data();
        }
        return result;
    }

    std::expected<std::vector<std::filesystem::path>, ExtractError> extract(
            const std::filesystem::path& archive_path,
            const std::filesystem::path& destination_path)
    {
        std::filesystem::create_directories(destination_path);

        // Phase 1: List contents (this is a safe read-only operation)
        std::string list_command = "tar -t -f " + archive_path.string();
        std::vector<std::filesystem::path> extracted_files;
        try {
            std::string file_list_str = exec_and_capture(list_command.c_str());
            std::stringstream ss(file_list_str);
            std::string line;
            while (std::getline(ss, line)) {
                if (line.empty() || line.back() == '/') continue;
                if (line.rfind("./", 0) == 0) line = line.substr(2);
                extracted_files.emplace_back(line);
            }
        } catch (const std::exception& e) {
            log::error("Failed to list archive contents: " + std::string(e.what()));
            return std::unexpected(ExtractError::ReadHeader);
        }

        // Phase 2: Extract the archive safely
        try {
            CWDGuard guard; // Automatically changes back to original path on scope exit.
            std::filesystem::current_path(destination_path);

            std::string command = "tar -x -f " + archive_path.string();

            int status = std::system(command.c_str());
            if (WEXITSTATUS(status) != 0) {
                log::error("tar extraction command failed with exit code: " + std::to_string(WEXITSTATUS(status)));
                return std::unexpected(ExtractError::ExtractData);
            }
        } catch (const std::exception& e) {
            log::error("Filesystem error during extraction: " + std::string(e.what()));
            return std::unexpected(ExtractError::InternalError);
        }

        return extracted_files;
    }


    std::expected<std::string, ExtractError> extract_single_file_to_memory(
            const std::filesystem::path& archive_path,
            const std::filesystem::path& file_inside_archive)
    {
        std::string path_in_archive = "./" + file_inside_archive.string();

        // The tar `-O` flag directs the output of the extracted file to stdout.
        std::string command = "tar -x -O -f " + archive_path.string() + " " + path_in_archive;

        try {
            // We need to check the return code of the command itself.
            // popen doesn't give us this directly, so we'll switch to a slightly different method.
            std::array<char, 128> buffer;
            std::string result;

            // Append stderr redirection to capture tar's error messages
            command += " 2>&1";

            FILE* pipe = popen(command.c_str(), "r");
            if (!pipe) {
                throw std::runtime_error("popen() failed!");
            }

            while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
                result += buffer.data();
            }

            int return_code = pclose(pipe);

            // WEXITSTATUS extracts the actual exit code from the status returned by pclose
            if (WEXITSTATUS(return_code) != 0) {
                // Tar failed. The 'result' string now contains its stderr message.
                log::error("tar command failed: " + result);
                return std::unexpected(ExtractError::ReadHeader);
            }

            return result;

        } catch (const std::exception& e) {
            log::error("Failed to extract '" + file_inside_archive.string() + "' from archive: " + std::string(e.what()));
            return std::unexpected(ExtractError::InternalError);
        }
    }

} // namespace au::archive