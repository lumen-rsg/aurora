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
        std::cout << color_code << "au :: [" << level << "] :: " << "\033[0m" << msg << std::endl;
    }

    inline void ok(const std::string& msg) {
        print("OK", "\033[1;32m", msg); // Bold Green
    }

    inline void error(const std::string& msg, const std::source_location& loc = std::source_location::current()) {
        std::string full_msg = msg + " (at " + loc.file_name() + ":" + std::to_string(loc.line()) + ")";
        print("ER", "\033[1;31m", full_msg); // Bold Red
    }

    inline void info(const std::string& msg) {
        print("..", "\033[1;34m", msg); // Bold Blue
    }

} // namespace au::log