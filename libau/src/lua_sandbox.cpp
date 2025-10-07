//
// Created by cv2 on 10/7/25.
//
#include "libau/lua_sandbox.h"
#include "libau/logging.h"
#include <lua.hpp> // The C++ wrapper for Lua headers
#include <fstream>
#include <sstream>

namespace au {

// --- C++ functions to be exposed to Lua ---

// Allows Lua scripts to call `aurora.info("some message")`
static int l_aurora_info(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    log::info(msg);
    return 0; // Number of return values
}

// Allows Lua scripts to call `aurora.warn("some message")`
static int l_aurora_warn(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    log::warn(msg);
    return 0;
}

// Our custom "aurora" library to be registered in Lua
static const luaL_Reg aurora_lib[] = {
    {"info", l_aurora_info},
    {"warn", l_aurora_warn},
    {NULL, NULL} // Sentinel
};

struct LuaSandbox::Impl {
    lua_State* L;

    Impl() {
        L = luaL_newstate();
        luaL_openlibs(L); // Open standard libraries

        // Create the 'aurora' table and register our C++ functions in it
        lua_newtable(L);
        luaL_setfuncs(L, aurora_lib, 0);
        lua_setglobal(L, "aurora"); // Make it available as a global 'aurora' table
    }

    ~Impl() {
        if (L) {
            lua_close(L);
        }
    }

    bool run(const std::string& script_content, const std::filesystem::path& target_root) {
        if (luaL_loadstring(L, script_content.c_str()) != LUA_OK) {
            log::error("Lua script compilation failed: " + std::string(lua_tostring(L, -1)));
            lua_pop(L, 1); // Pop the error message from the stack
            return false;
        }

        // --- THE SANDBOX ---
        lua_newtable(L); // Create the environment table for our script

        // Whitelist safe functions from the global scope
        const char* whitelist[] = {"print", "ipairs", "pairs", "next", "tostring", "tonumber", "type", "aurora", "string", "table", "math"};
        for (const char* lib : whitelist) {
            lua_getglobal(L, lib);
            lua_setfield(L, -2, lib); // env[lib] = _G[lib]
        }

        // Set the script's environment
        lua_setupvalue(L, 1, 1); // Set the first upvalue (_ENV) of the loaded chunk

        // Push the target_root as the first argument to the script
        lua_pushstring(L, target_root.c_str());

        // Execute the script: 1 argument, 0 return values
        if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
            log::error("Lua script execution failed: " + std::string(lua_tostring(L, -1)));
            lua_pop(L, 1); // Pop the error message
            return false;
        }

        return true;
    }
};

// --- Public Method Implementations ---
LuaSandbox::LuaSandbox() : pimpl(std::make_unique<Impl>()) {}
LuaSandbox::~LuaSandbox() = default;

bool LuaSandbox::run_script(const std::string& script_content, const std::filesystem::path& target_root) {
    return pimpl->run(script_content, target_root);
}

bool LuaSandbox::run_script_from_file(const std::filesystem::path& script_path, const std::filesystem::path& target_root) {
    std::ifstream file(script_path);
    if (!file.is_open()) {
        log::error("Failed to open script file: " + script_path.string());
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return pimpl->run(buffer.str(), target_root);
}

} // namespace au