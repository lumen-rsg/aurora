//
// Created by cv2 on 10/7/25.
//

#pragma once

#include <filesystem>
#include <string>
#include <memory>

// Forward declare the Lua state
struct lua_State;

namespace au {

    class LuaSandbox {
    public:
        LuaSandbox();
        ~LuaSandbox();

        // Executes a script from a string.
        // Returns true on success, false on script error.
        bool run_script(const std::string& script_content, const std::filesystem::path& target_root);

        // Executes a script from a file path.
        // Returns true on success, false on file-not-found or script error.
        bool run_script_from_file(const std::filesystem::path& script_path, const std::filesystem::path& target_root);

    private:
        // PIMPL to hide the lua_State* from the header
        struct Impl;
        std::unique_ptr<Impl> pimpl;
    };

} // namespace au