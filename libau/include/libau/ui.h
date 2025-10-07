#pragma once

#include <unistd.h> // For isatty and STDOUT_FILENO

namespace au::ui {

    /**
     * @brief Checks if standard output is connected to an interactive terminal (TTY).
     * @return True if output is interactive, false otherwise (e.g., piped or redirected to a file).
     */
    inline bool is_interactive() {
        // isatty() returns 1 if the file descriptor is a terminal, 0 otherwise.
        return isatty(STDOUT_FILENO) != 0;
    }

} // namespace au::ui