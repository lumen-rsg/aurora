//
// Created by cv2 on 8/14/25.
//

#pragma once

#include <iostream>
#include <string>
#include <source_location> // C++20, but essential for good logging

namespace au::log {

    // Helper function to format the output consistently
    inline void print(const std::string& level, const std::string& color_code, const std::string& msg) {
        std::cout << color_code << "[  " << level << "  ] > " << "\033[0m" << msg << std::endl;
    }

    inline void ok(const std::string& msg) {
        print("OKY", "\033[1;32m", msg); // Bold Green
    }

    inline void error(const std::string& msg, const std::source_location& loc = std::source_location::current()) {
        std::string full_msg = msg + " (at " + loc.file_name() + ":" + std::to_string(loc.line()) + ")";
        print("ERR", "\033[1;31m", full_msg); // Bold Red
    }

    inline void info(const std::string& msg) {
        print("LOG", "\033[1;34m", msg); // Bold Blue
    }

    inline void warn(const std::string& msg) {
        print("WRN", "\033[1;33m", msg); // Bold Yellow
    }

    // Prints a progress message without a newline, and flushes the output.
    // This is for showing what is currently happening.
    inline void progress(const std::string& msg) {
        // \r: Carriage return (moves cursor to the beginning of the line)
        // \033[K: Erase from the cursor to the end of the line
        std::cout << "\r\033[K"
                  << "\033[1;34m" << "[..] > " << "\033[0m" // Blue header
                  << msg << std::flush;
    }

    // Prints a green "[OK]" message and finally moves to the next line.
    // This is for showing that a series of progress steps has completed.
    inline void progress_ok() {
        std::cout << " [" << "\033[1;32m" << "  OKY  " << "\033[0m" << "]" << std::endl;
    }

} // namespace au::log