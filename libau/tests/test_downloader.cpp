//
// Created by cv2 on 8/15/25.
//
#include "downloader.h"
#include "logging.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <vector>
#include <numeric>

// --- Test Fixture for managing a temporary download directory ---
struct DownloaderTestFixture {
    const std::filesystem::path download_target_dir;

    DownloaderTestFixture() :
            download_target_dir("/tmp/aurora_downloader_test")
    {
        std::filesystem::remove_all(download_target_dir);
        std::filesystem::create_directories(download_target_dir);
    }

    ~DownloaderTestFixture() {
        std::filesystem::remove_all(download_target_dir);
    }
};


void test_parallel_download_success() {
    au::log::info("Running test: Parallel Download Success");
    DownloaderTestFixture fixture;
    au::Downloader downloader;

    // Setup: Define two valid download jobs
    std::vector<au::DownloadJob> jobs;
    jobs.emplace_back("http://127.0.0.1:8000/dummy_file_1.bin",
                      fixture.download_target_dir / "file1.bin",
                      "dummy_file_1");
    jobs.emplace_back("http://127.0.0.1:8000/dummy_file_2.bin",
                      fixture.download_target_dir / "file2.bin",
                      "dummy_file_2");

    // Action
    bool success = downloader.download_all(jobs);

    // Verification
    assert(success == true);
    assert(std::filesystem::exists(fixture.download_target_dir / "file1.bin"));
    assert(std::filesystem::exists(fixture.download_target_dir / "file2.bin"));
    assert(std::filesystem::file_size(fixture.download_target_dir / "file1.bin") == 1024 * 1024);
    assert(std::filesystem::file_size(fixture.download_target_dir / "file2.bin") == 2048 * 1024);
    assert(jobs[0].error_message.empty());
    assert(jobs[1].error_message.empty());

    au::log::ok("Test Passed: Parallel Download Success");
}

void test_download_with_failure() {
    au::log::info("Running test: Download with one failure (404)");
    DownloaderTestFixture fixture;
    au::Downloader downloader;

    // Setup: Define one valid job and one that will fail (404 Not Found)
    std::vector<au::DownloadJob> jobs;
    jobs.emplace_back("http://127.0.0.1:8000/dummy_file_1.bin",
                      fixture.download_target_dir / "good_file.bin",
                      "good_file");
    jobs.emplace_back("http://127.0.0.1:8000/non_existent_file.bin",
                      fixture.download_target_dir / "bad_file.bin",
                      "bad_file");

    // Action
    bool success = downloader.download_all(jobs);

    // Verification
    assert(success == false); // The overall operation should report failure

    // Check the successful job
    assert(std::filesystem::exists(fixture.download_target_dir / "good_file.bin"));
    assert(std::filesystem::file_size(fixture.download_target_dir / "good_file.bin") == 1024 * 1024);
    assert(jobs[0].error_message.empty());

    // Check the failed job
    assert(!std::filesystem::exists(fixture.download_target_dir / "bad_file.bin"));
    assert(!jobs[1].error_message.empty()); // Should have an error message
    // A 404 error from curl often includes "HTTP response code said error" or similar
    assert(jobs[1].error_message.find("HTTP") != std::string::npos);

    au::log::ok("Test Passed: Download with Failure");
}

int main() {
    au::log::info("--- Downloader Test Suite ---");
    au::log::info("IMPORTANT: Please run a web server in 'aurora/libau/tests/test_downloader_setup/'");
    au::log::info("Example: python3 -m http.server 8000");
    au::log::info("---------------------------------");

    try {
        test_parallel_download_success();
        test_download_with_failure();
    } catch (const std::exception& e) {
        au::log::error(std::string("A downloader test failed with an exception: ") + e.what());
        return 1;
    }

    au::log::ok("All downloader tests completed successfully!");
    return 0;
}