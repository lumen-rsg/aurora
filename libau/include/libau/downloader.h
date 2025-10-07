//
// Created by cv2 on 8/14/25.
//


#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <memory>

namespace au {

// Represents a single download task and its live progress.
    struct DownloadJob {
        // --- Inputs ---

        std::vector<std::string> urls;
        std::filesystem::path destination_path;
        std::string name_for_display;

        // --- Live State (Outputs) ---
        double total_size_bytes = 0.0;
        double downloaded_bytes = 0.0;
        double current_speed_bps = 0.0;
        bool finished = false;
        std::string error_message;

        // An explicit constructor to initialize the public "input" members.
        // We use std::move to be efficient with the string arguments.
        DownloadJob(std::vector<std::string> u, std::filesystem::path d, std::string n)
                : urls(std::move(u)),
                  destination_path(std::move(d)),
                  name_for_display(std::move(n))
        {}

    private:
        friend class Downloader;
        void* internal_handle = nullptr;
        FILE* file_handle = nullptr;
    };

// A parallel downloader for multiple DownloadJob objects.
    class Downloader {
    public:
        Downloader();
        ~Downloader();

        // Downloads all jobs in parallel and updates their state in-place.
        // Returns true if ALL downloads succeeded, false otherwise.
        bool download_all(std::vector<DownloadJob>& jobs);

        int64_t get_total_download_size(const std::vector<DownloadJob> &jobs);

    private:
        struct Impl;
        std::unique_ptr<Impl> pimpl;
    };

} // namespace au

