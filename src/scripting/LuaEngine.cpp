/**
 * @file LuaEngine.cpp
 * @brief LuaJIT scripting engine implementation.
 *
 * Uses LuaJIT (Lua 5.1 compatible, JIT compiled for 10-50x speed over
 * interpreted Lua). The C API is identical to standard Lua 5.1.
 *
 * Used for: world events, victory conditions, building/unit special abilities,
 * AI personality overrides, map generation scripts, and mod init scripts.
 */

#include "aoc/scripting/LuaEngine.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/core/Log.hpp"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <string>

namespace aoc::scripting {

namespace {

// Sandbox replacement for load/loadstring: string chunks only, and the
// chunk mode is forced to "t" (text). LuaJIT executes unverified bytecode
// by design -- a crafted bytecode chunk is a VM escape (luajit.org FAQ) --
// so the mode argument callers pass is deliberately ignored. Keeps the
// common data-mod idiom load("return {...}") working.
int textOnlyLoad(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING) {
        return luaL_error(L, "load: only text string chunks are allowed in the sandbox");
    }
    std::size_t len = 0;
    const char* buf = lua_tolstring(L, 1, &len);
    const char* name = luaL_optstring(L, 2, "=(load)");
    if (luaL_loadbufferx(L, buf, len, name, "t") != 0) {
        // load() contract on failure: return nil + error message.
        lua_pushnil(L);
        lua_insert(L, -2);
        return 2;
    }
    return 1;
}

} // namespace

struct LuaEngine::Impl {
    lua_State* luaState = nullptr;
    aoc::game::GameState* gameState = nullptr;
    aoc::map::HexGrid* grid = nullptr;
};

LuaEngine::LuaEngine() : m_impl(std::make_unique<Impl>()) {}

LuaEngine::~LuaEngine() {
    if (this->m_impl != nullptr && this->m_impl->luaState != nullptr) {
        lua_close(this->m_impl->luaState);
    }
}

LuaEngine::LuaEngine(LuaEngine&&) noexcept = default;
LuaEngine& LuaEngine::operator=(LuaEngine&& other) noexcept {
    if (this != &other) {
        if (this->m_impl != nullptr && this->m_impl->luaState != nullptr) {
            lua_close(this->m_impl->luaState);
        }
        this->m_impl = std::move(other.m_impl);
    }
    return *this;
}

bool LuaEngine::initialize(const std::string& scriptsPath) {
    this->m_impl->luaState = luaL_newstate();
    if (this->m_impl->luaState == nullptr) {
        LOG_ERROR("LuaEngine::initialize: failed to create Lua state");
        return false;
    }

    // Sandbox: open ONLY the safe standard libraries (base, string, table,
    // math, and utf8 where available). Mods are untrusted scripts that we
    // load from data/scripts/ and data/mods/, so the full standard library
    // would expose io.open / os.execute / package.loadlib / require /
    // dofile / loadfile / debug.* — every one of these turns a malicious
    // script into RCE on the host machine.
    //
    // LuaJIT 2.1 follows the Lua 5.1 C API and does NOT provide
    // luaL_requiref (added in Lua 5.2) or luaopen_utf8 (added in Lua 5.3).
    // For that path we replicate the Lua 5.1 idiom: push the loader,
    // push the module name, call it, and discard the returned table —
    // luaL_openlibs() does exactly this internally.
    lua_State* L = this->m_impl->luaState;
#if LUA_VERSION_NUM >= 502
    luaL_requiref(L, "_G",     luaopen_base,   1); lua_pop(L, 1);
    luaL_requiref(L, "string", luaopen_string, 1); lua_pop(L, 1);
    luaL_requiref(L, "table",  luaopen_table,  1); lua_pop(L, 1);
    luaL_requiref(L, "math",   luaopen_math,   1); lua_pop(L, 1);
#if LUA_VERSION_NUM >= 503
    luaL_requiref(L, "utf8",   luaopen_utf8,   1); lua_pop(L, 1);
#endif
#else
    // LuaJIT / Lua 5.1 path: invoke each loader as a Lua function with
    // its module name on the stack, mirroring lj_lib.c's openlib helper.
    lua_pushcfunction(L, luaopen_base);   lua_pushstring(L, "");      lua_call(L, 1, 0);
    lua_pushcfunction(L, luaopen_string); lua_pushstring(L, "string"); lua_call(L, 1, 0);
    lua_pushcfunction(L, luaopen_table);  lua_pushstring(L, "table");  lua_call(L, 1, 0);
    lua_pushcfunction(L, luaopen_math);   lua_pushstring(L, "math");   lua_call(L, 1, 0);
    // utf8 lib is not shipped by LuaJIT 2.1 — skip on this path.
#endif

    // Defence in depth: nil out anything an attacker could reach via the
    // base library or that a future luaL_openlibs change might re-expose.
    static constexpr const char* kBlockedGlobals[] = {
        "os", "io", "package", "require", "dofile", "loadfile", "debug",
    };
    for (const char* name : kBlockedGlobals) {
        lua_pushnil(L);
        lua_setglobal(L, name);
    }

    // load/loadstring stay usable (data mods rely on load("return {...}"))
    // but are replaced with a wrapper that forces text-only chunks --
    // LuaJIT has no bytecode verifier, so accepting bytecode here would be
    // a sandbox escape regardless of the blocked globals above.
    lua_pushcfunction(L, textOnlyLoad);
    lua_setglobal(L, "load");
    lua_pushcfunction(L, textOnlyLoad);
    lua_setglobal(L, "loadstring");

    // Load init script if it exists
    std::string initPath = scriptsPath + "/init.lua";
    if (this->executeFile(initPath)) {
        LOG_INFO("Lua scripting initialized from %s", scriptsPath.c_str());
    }

    return true;
}

bool LuaEngine::isAvailable() const {
    return this->m_impl != nullptr && this->m_impl->luaState != nullptr;
}

bool LuaEngine::executeFile(const std::string& path) {
    if (!this->isAvailable()) { return false; }

    // Text-only ("t"): LuaJIT runs unverified bytecode, so a .lua file
    // holding a crafted bytecode dump would escape the sandbox.
    lua_State* L = this->m_impl->luaState;
    int result = luaL_loadfilex(L, path.c_str(), "t");
    if (result == LUA_OK) {
        result = lua_pcall(L, 0, LUA_MULTRET, 0);
    }
    if (result != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        LOG_ERROR("Lua script error in %s: %s", path.c_str(), err != nullptr ? err : "unknown");
        lua_pop(L, 1);
        return false;
    }
    return true;
}

bool LuaEngine::executeString(std::string_view code) {
    if (!this->isAvailable()) { return false; }

    // Text-only ("t") for the same reason as executeFile.
    lua_State* L = this->m_impl->luaState;
    int result = luaL_loadbufferx(L, code.data(), code.size(), "=eval", "t");
    if (result == LUA_OK) {
        result = lua_pcall(L, 0, LUA_MULTRET, 0);
    }
    if (result != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        LOG_ERROR("Lua eval error: %s", err != nullptr ? err : "unknown");
        lua_pop(L, 1);
        return false;
    }
    return true;
}

void LuaEngine::bindGameState(aoc::game::GameState& gameState, aoc::map::HexGrid& grid) {
    this->m_impl->gameState = &gameState;
    this->m_impl->grid = &grid;

    lua_State* L = this->m_impl->luaState;
    if (L == nullptr) { return; }

    // Create 'game' table with basic info
    lua_newtable(L);
    lua_pushinteger(L, static_cast<lua_Integer>(grid.width()));
    lua_setfield(L, -2, "mapWidth");
    lua_pushinteger(L, static_cast<lua_Integer>(grid.height()));
    lua_setfield(L, -2, "mapHeight");
    lua_setglobal(L, "game");
}

bool LuaEngine::callFunction(std::string_view funcName) {
    if (!this->isAvailable()) { return false; }

    std::string name(funcName);
    lua_getglobal(this->m_impl->luaState, name.c_str());
    if (!lua_isfunction(this->m_impl->luaState, -1)) {
        lua_pop(this->m_impl->luaState, 1);
        return false;
    }

    if (lua_pcall(this->m_impl->luaState, 0, 1, 0) != LUA_OK) {
        const char* err = lua_tostring(this->m_impl->luaState, -1);
        LOG_ERROR("Lua call '%s' failed: %s", name.c_str(), err != nullptr ? err : "unknown");
        lua_pop(this->m_impl->luaState, 1);
        return false;
    }

    bool result = lua_toboolean(this->m_impl->luaState, -1) != 0;
    lua_pop(this->m_impl->luaState, 1);
    return result;
}

bool LuaEngine::callFunctionWithPlayer(std::string_view funcName, PlayerId player) {
    if (!this->isAvailable()) { return false; }

    std::string name(funcName);
    lua_getglobal(this->m_impl->luaState, name.c_str());
    if (!lua_isfunction(this->m_impl->luaState, -1)) {
        lua_pop(this->m_impl->luaState, 1);
        return false;
    }

    lua_pushinteger(this->m_impl->luaState, static_cast<lua_Integer>(player));
    if (lua_pcall(this->m_impl->luaState, 1, 1, 0) != LUA_OK) {
        const char* err = lua_tostring(this->m_impl->luaState, -1);
        LOG_ERROR("Lua call '%s(%u)' failed: %s", name.c_str(),
                  static_cast<unsigned>(player), err != nullptr ? err : "unknown");
        lua_pop(this->m_impl->luaState, 1);
        return false;
    }

    bool result = lua_toboolean(this->m_impl->luaState, -1) != 0;
    lua_pop(this->m_impl->luaState, 1);
    return result;
}

bool LuaEngine::callFunctionWithTurn(std::string_view funcName, int32_t turn) {
    if (!this->isAvailable()) { return false; }

    std::string name(funcName);
    lua_getglobal(this->m_impl->luaState, name.c_str());
    if (!lua_isfunction(this->m_impl->luaState, -1)) {
        lua_pop(this->m_impl->luaState, 1);
        return false;
    }

    lua_pushinteger(this->m_impl->luaState, static_cast<lua_Integer>(turn));
    if (lua_pcall(this->m_impl->luaState, 1, 1, 0) != LUA_OK) {
        const char* err = lua_tostring(this->m_impl->luaState, -1);
        LOG_ERROR("Lua call '%s(%d)' failed: %s", name.c_str(), turn,
                  err != nullptr ? err : "unknown");
        lua_pop(this->m_impl->luaState, 1);
        return false;
    }

    bool result = lua_toboolean(this->m_impl->luaState, -1) != 0;
    lua_pop(this->m_impl->luaState, 1);
    return result;
}

void LuaEngine::registerFunction(std::string_view name, int32_t (*func)(void*)) {
    (void)name;
    (void)func;
    // Full implementation: wrap the function pointer in a lua_CFunction adapter
    // and register via lua_pushcfunction + lua_setglobal
}

} // namespace aoc::scripting
