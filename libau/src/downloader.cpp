//
// Created by cv2 on 8/14/25.
//

#include "downloader.h"
#include "logging.h"
#include <curl/multi.h>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <thread> // for sleep_for
#include <map>

namespace au {

// Converts bytes to a human-readable string (KB, MB, GB)
    std::string format_bytes(double bytes) {
        const char* suffixes[] = {"B", "KB", "MB", "GB"};
        int suffix_index = 0;
        while (bytes >= 1024 && suffix_index < 3) {
            bytes /= 1024;
            suffix_index++;
        }
        std::stringstream ss;
        ss << std::fixed << std::setprecision(1) << bytes << " " << suffixes[suffix_index];
        return ss.str();
    }

// Renders the progress bars for all current downloads.
// Uses ANSI escape codes to overwrite the previous output.
    void print_progress_bars(const std::vector<DownloadJob>& jobs, bool first_print) {
        if (!first_print) {
            // Move cursor up N lines, where N is the number of jobs
            std::cout << "\033[" << jobs.size() << "A";
        }
        for (const auto& job : jobs) {
            double percentage = (job.total_size_bytes > 0) ? (job.downloaded_bytes / job.total_size_bytes) * 100.0 : 0.0;

            std::cout << "\r\033[K"; // Erase the current line
            std::cout << std::left << std::setw(25) << job.name_for_display << " ";

            if (!job.error_message.empty()) {
                std::cout << "\033[1;31mError: " << job.error_message << "\033[0m" << std::endl;
                continue;
            }

            if (job.finished) {
                std::cout << format_bytes(job.total_size_bytes) << " [\033[1;32mFinished\033[0m]" << std::endl;
                continue;
            }

            std::cout << std::right << std::setw(8) << format_bytes(job.total_size_bytes) << " ";

            // Progress bar
            const int bar_width = 20;
            int pos = static_cast<int>(bar_width * percentage / 100.0);
            std::cout << "[";
            for (int i = 0; i < bar_width; ++i) {
                if (i < pos) std::cout << "=";
                else if (i == pos) std::cout << ">";
                else std::cout << " ";
            }
            std::cout << "] ";

            std::cout << std::fixed << std::setprecision(1) << std::setw(5) << percentage << "% ";
            std::cout << std::setw(10) << format_bytes(job.current_speed_bps) << "/s" << std::endl;
        }
        std::cout.flush();
    }

// --- libcurl Callbacks ---

// The write callback for saving data to a file
    static size_t write_callback(void* ptr, size_t size, size_t nmemb, FILE* stream) {
        return fwrite(ptr, size, nmemb, stream);
    }

// The progress callback for updating a DownloadJob's state
    static int progress_callback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
        auto* job = static_cast<DownloadJob*>(clientp);
        job->total_size_bytes = static_cast<double>(dltotal);
        job->downloaded_bytes = static_cast<double>(dlnow);
        return 0; // Returning non-zero would abort the transfer
    }


// --- PIMPL Implementation ---
    struct Downloader::Impl {
        CURLM* multi_handle;

        Impl() {
            curl_global_init(CURL_GLOBAL_ALL);
            multi_handle = curl_multi_init();
        }

        ~Impl() {
            curl_multi_cleanup(multi_handle);
            curl_global_cleanup();
        }

        bool download_all(std::vector<DownloadJob>& jobs) {
            if (jobs.empty()) return true;

            for (auto& job : jobs) {
                CURL* easy_handle = curl_easy_init();
                job.internal_handle = easy_handle;
                job.file_handle = fopen(job.destination_path.c_str(), "wb");
                if (!job.file_handle) {
                    job.error_message = "Could not open file for writing.";
                    job.finished = true;
                    continue;
                }

                curl_easy_setopt(easy_handle, CURLOPT_URL, job.url.c_str());
                curl_easy_setopt(easy_handle, CURLOPT_WRITEFUNCTION, write_callback);
                curl_easy_setopt(easy_handle, CURLOPT_WRITEDATA, job.file_handle);
                curl_easy_setopt(easy_handle, CURLOPT_NOPROGRESS, 0L);
                curl_easy_setopt(easy_handle, CURLOPT_XFERINFOFUNCTION, progress_callback);
                curl_easy_setopt(easy_handle, CURLOPT_XFERINFODATA, &job);
                curl_easy_setopt(easy_handle, CURLOPT_FOLLOWLOCATION, 1L);
                curl_easy_setopt(easy_handle, CURLOPT_FAILONERROR, 1L);

                curl_multi_add_handle(multi_handle, easy_handle);
            }

            int still_running = 0;
            bool first_print = true;
            auto last_time = std::chrono::steady_clock::now();
            std::map<void*, double> last_downloaded_bytes;

            do {
                curl_multi_perform(multi_handle, &still_running);

                auto current_time = std::chrono::steady_clock::now();
                double time_delta = std::chrono::duration<double>(current_time - last_time).count();

                if (time_delta >= 0.5) { // Update UI and speed every 0.5 seconds
                    for (auto& job : jobs) {
                        if (!job.finished) {
                            job.current_speed_bps = (job.downloaded_bytes - last_downloaded_bytes[job.internal_handle]) / time_delta;
                            last_downloaded_bytes[job.internal_handle] = job.downloaded_bytes;
                        }
                    }
                    print_progress_bars(jobs, first_print);
                    first_print = false;
                    last_time = current_time;
                }

                // Check for completed transfers
                CURLMsg* msg;
                int msgs_left;
                while ((msg = curl_multi_info_read(multi_handle, &msgs_left))) {
                    if (msg->msg == CURLMSG_DONE) {
                        CURL* easy_handle = msg->easy_handle;
                        for (auto& job : jobs) {
                            if (job.internal_handle == easy_handle) {
                                job.finished = true;
                                job.current_speed_bps = 0;
                                if (msg->data.result != CURLE_OK) {
                                    job.error_message = curl_easy_strerror(msg->data.result);
                                }
                                break;
                            }
                        }
                        curl_multi_remove_handle(multi_handle, easy_handle);
                    }
                }

                if (still_running) {
                    curl_multi_poll(multi_handle, NULL, 0, 100, NULL);
                }

            } while (still_running > 0);

            print_progress_bars(jobs, first_print);

            bool all_ok = true;
            for (auto& job : jobs) {
                if (job.file_handle) {
                    fclose(job.file_handle);
                }
                if (job.internal_handle) {
                    curl_easy_cleanup(static_cast<CURL*>(job.internal_handle));
                }

                if (!job.error_message.empty()) {
                    all_ok = false;
                    // If the download failed, we must remove the empty or partially-written file
                    // to ensure the filesystem is left in a clean state.
                    std::error_code ec;
                    std::filesystem::remove(job.destination_path, ec);
                    if (ec) {
                        log::error("Failed to clean up partial download: " + job.destination_path.string());
                    }
                    // -----------------------
                }
            }

            return all_ok;
        }
    };

// --- Public Class Implementation ---
    Downloader::Downloader() : pimpl(std::make_unique<Impl>()) {}
    Downloader::~Downloader() = default;

    bool Downloader::download_all(std::vector<DownloadJob>& jobs) {
        return pimpl->download_all(jobs);
    }

} // namespace au

