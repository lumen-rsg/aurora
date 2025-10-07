//
// Created by cv2 on 8/14/25.
//

#include "libau/downloader.h"
#include "libau/logging.h"
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

        // NEW: Track the current mirror index for each active download handle
        std::map<CURL*, size_t> mirror_tracker;

        for (auto& job : jobs) {
            if (job.urls.empty()) {
                job.error_message = "No source URLs provided.";
                job.finished = true;
                continue;
            }

            CURL* easy_handle = curl_easy_init();
            job.internal_handle = easy_handle;
            mirror_tracker[easy_handle] = 0; // Start with the first mirror (index 0)

            job.file_handle = fopen(job.destination_path.c_str(), "wb");
            if (!job.file_handle) {
                job.error_message = "Could not open file for writing.";
                job.finished = true;
                curl_easy_cleanup(easy_handle); // Clean up the handle we just created
                job.internal_handle = nullptr;
                continue;
            }

            // Set all curl options, using the FIRST mirror URL to start.
            curl_easy_setopt(easy_handle, CURLOPT_URL, job.urls[0].c_str());
            curl_easy_setopt(easy_handle, CURLOPT_WRITEFUNCTION, write_callback);
            curl_easy_setopt(easy_handle, CURLOPT_WRITEDATA, job.file_handle);
            curl_easy_setopt(easy_handle, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(easy_handle, CURLOPT_XFERINFOFUNCTION, progress_callback);
            curl_easy_setopt(easy_handle, CURLOPT_XFERINFODATA, &job);
            curl_easy_setopt(easy_handle, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(easy_handle, CURLOPT_FAILONERROR, 1L); // Fail on HTTP >= 400

            curl_multi_add_handle(multi_handle, easy_handle);
        }

        int still_running = 0;
        bool first_print = true;
        auto last_time = std::chrono::steady_clock::now();
        std::map<void*, double> last_downloaded_bytes;

        if (!au::ui::is_interactive()) {
            for(const auto& job : jobs) {
                if (!job.finished) { // Check in case a job was pre-failed
                    log::info("Beginning download for " + job.name_for_display);
                }
            }
        }

        do {
            curl_multi_perform(multi_handle, &still_running);

            auto current_time = std::chrono::steady_clock::now();
            double time_delta = std::chrono::duration<double>(current_time - last_time).count();

            if (au::ui::is_interactive() && time_delta >= 0.5) { // Update UI and speed every 0.5 seconds
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

                    // --- START OF MIRROR LOGIC ---
                    if (msg->data.result == CURLE_OK) {
                        // SUCCESS: Mark job as finished and clean up.
                        for (auto& job : jobs) {
                            if (job.internal_handle == easy_handle) {
                                job.finished = true;
                                job.current_speed_bps = 0;
                                break;
                            }
                        }
                        curl_multi_remove_handle(multi_handle, easy_handle);
                        mirror_tracker.erase(easy_handle);
                    } else {
                        // FAILURE: Try the next mirror if available.
                        size_t& current_mirror_idx = mirror_tracker.at(easy_handle);
                        current_mirror_idx++;

                        DownloadJob* current_job = nullptr;
                        for (auto& j : jobs) { if (j.internal_handle == easy_handle) { current_job = &j; break; } }

                        if (current_job && current_mirror_idx < current_job->urls.size()) {
                            // There's another mirror to try.
                            log::warn("Download for '" + current_job->name_for_display + "' failed. Trying next mirror...");

                            // Remove the handle first before reconfiguring
                            curl_multi_remove_handle(multi_handle, easy_handle);

                            // Reopen the file to write from the beginning
                            fclose(current_job->file_handle);
                            current_job->file_handle = fopen(current_job->destination_path.c_str(), "wb");

                            // Reset state for the new transfer
                            current_job->downloaded_bytes = 0;
                            last_downloaded_bytes[easy_handle] = 0;

                            // Reconfigure the handle with the new URL and file
                            curl_easy_setopt(easy_handle, CURLOPT_URL, current_job->urls[current_mirror_idx].c_str());
                            curl_easy_setopt(easy_handle, CURLOPT_WRITEDATA, current_job->file_handle);

                            // Re-add the handle to start the transfer again
                            curl_multi_add_handle(multi_handle, easy_handle);
                        } else {
                            // NO MORE MIRRORS: This job has officially failed.
                            for (auto& job : jobs) {
                                if (job.internal_handle == easy_handle) {
                                    job.finished = true;
                                    job.current_speed_bps = 0;
                                    job.error_message = curl_easy_strerror(msg->data.result);
                                    break;
                                }
                            }
                            curl_multi_remove_handle(multi_handle, easy_handle);
                            mirror_tracker.erase(easy_handle);
                        }
                    }
                    if (!au::ui::is_interactive()) {
                        for (const auto& job : jobs) {
                            if (job.internal_handle == msg->easy_handle) {
                                if (msg->data.result == CURLE_OK) {
                                    log::ok("Download complete: " + job.name_for_display);
                                } else {
                                    log::error("Download failed: " + job.name_for_display);
                                }
                                break;
                            }
                        }
                    }
                }
            }

            if (still_running) {
                curl_multi_poll(multi_handle, NULL, 0, 100, NULL);
            }

        } while (still_running > 0);

        if (au::ui::is_interactive()) {
            print_progress_bars(jobs, first_print);
        }


        bool all_ok = true;
        for (auto& job : jobs) {
            if (job.file_handle) {
                fclose(job.file_handle);
                job.file_handle = nullptr; // Prevent double-free
            }
            if (job.internal_handle) {
                curl_easy_cleanup(static_cast<CURL*>(job.internal_handle));
            }

            if (!job.error_message.empty()) {
                all_ok = false;
                // If the download failed, clean up the partial file.
                std::error_code ec;
                std::filesystem::remove(job.destination_path, ec);
                if (ec) {
                    log::error("Failed to clean up partial download: " + job.destination_path.string());
                }
            }
        }

        return all_ok;
    }

        int64_t get_total_download_size(const std::vector<DownloadJob>& jobs) {
            CURLM *multi_handle_head = curl_multi_init();
            if (!multi_handle_head) return -1;

            for (const auto& job : jobs) {
                if (job.urls.empty()) continue;
                CURL *easy_handle = curl_easy_init();
                // We only try the first mirror for the size check for simplicity and speed.
                curl_easy_setopt(easy_handle, CURLOPT_URL, job.urls[0].c_str());
                curl_easy_setopt(easy_handle, CURLOPT_NOBODY, 1L); // This makes it a HEAD request
                curl_easy_setopt(easy_handle, CURLOPT_FOLLOWLOCATION, 1L);
                curl_multi_add_handle(multi_handle_head, easy_handle);
            }

            int still_running = 0;
            do {
                curl_multi_perform(multi_handle_head, &still_running);
                curl_multi_poll(multi_handle_head, NULL, 0, 100, NULL);
            } while (still_running > 0);

            int64_t total_size = 0;
            CURLMsg* msg;
            int msgs_left;
            while ((msg = curl_multi_info_read(multi_handle_head, &msgs_left))) {
                if (msg->msg == CURLMSG_DONE) {
                    CURL* easy_handle = msg->easy_handle;
                    if (msg->data.result == CURLE_OK) {
                        curl_off_t file_size = 0;
                        curl_easy_getinfo(easy_handle, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &file_size);
                        total_size += file_size;
                    } else {
                        // If any HEAD request fails, we can't get a reliable total.
                        total_size = -1;
                        break;
                    }
                    curl_multi_remove_handle(multi_handle_head, easy_handle);
                    curl_easy_cleanup(easy_handle);
                }
            }

            curl_multi_cleanup(multi_handle_head);
            return total_size;
        }
    };

// --- Public Class Implementation ---
    Downloader::Downloader() : pimpl(std::make_unique<Impl>()) {}
    Downloader::~Downloader() = default;

    bool Downloader::download_all(std::vector<DownloadJob>& jobs) {
        return pimpl->download_all(jobs);
    }

    int64_t Downloader::get_total_download_size(const std::vector<DownloadJob>& jobs) {
        return pimpl->get_total_download_size(jobs);
    }

} // namespace au

