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

// LuaJIT / Lua 5.1 predate LUA_OK (introduced in Lua 5.2); on those ABIs a
// successful call simply returns 0. Define it so the status checks below work
// uniformly across both interpreters.
#ifndef LUA_OK
#define LUA_OK 0
#endif

#include <string>

namespace aoc::scripting {

namespace {

/// Instruction budget for a single Lua call before the watchdog hook aborts it.
/// Untrusted mod scripts can otherwise spin in an infinite loop and hang the
/// host. 10M instructions is generous for legitimate event/victory scripts yet
/// trips in well under a second on a runaway loop.
constexpr int LUA_INSTRUCTION_LIMIT = 10'000'000;

/// LUA_MASKCOUNT hook: invoked every LUA_INSTRUCTION_LIMIT VM instructions.
/// Raising a Lua error here unwinds back to the enclosing pcall, so a runaway
/// script is reported as a failure rather than hanging the process. Memory
/// bombs are not bounded by this hook -- a capped allocator was deemed
/// higher-risk for the LuaJIT path and intentionally omitted (see WP-04 notes).
void instructionLimitHook(lua_State* L, lua_Debug* /*ar*/) {
    luaL_error(L, "script exceeded instruction limit (%d) -- possible infinite loop",
               LUA_INSTRUCTION_LIMIT);
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
    // Re-initialising must not leak the previous interpreter.
    if (this->m_impl->luaState != nullptr) {
        lua_close(this->m_impl->luaState);
        this->m_impl->luaState = nullptr;
    }

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
    // load/loadstring compile arbitrary bytecode (a sandbox escape), coroutine
    // can stall the scheduler, and jit.* (LuaJIT) exposes interpreter
    // internals -- none are needed by mod scripts.
    static constexpr const char* kBlockedGlobals[] = {
        "os", "io", "package", "require", "dofile", "loadfile", "debug",
        "load", "loadstring", "coroutine", "jit",
    };
    for (const char* name : kBlockedGlobals) {
        lua_pushnil(L);
        lua_setglobal(L, name);
    }

    // Resource limit: abort runaway scripts via an instruction-count watchdog.
    // The hook fires every LUA_INSTRUCTION_LIMIT VM instructions and raises a
    // Lua error, which unwinds back to the enclosing pcall.
    lua_sethook(L, instructionLimitHook, LUA_MASKCOUNT, LUA_INSTRUCTION_LIMIT);

    // Verify the sandbox holds after registering the approved API: any blocked
    // global that is still non-nil here would be an escape hatch.
    for (const char* name : kBlockedGlobals) {
        lua_getglobal(L, name);
        bool present = !lua_isnil(L, -1);
        lua_pop(L, 1);
        if (present) {
            LOG_ERROR("LuaEngine::initialize: blocked global '%s' is still reachable -- sandbox compromised", name);
            lua_close(L);
            this->m_impl->luaState = nullptr;
            return false;
        }
    }

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

    int result = luaL_dofile(this->m_impl->luaState, path.c_str());
    if (result != LUA_OK) {
        const char* err = lua_tostring(this->m_impl->luaState, -1);
        LOG_ERROR("Lua script error in %s: %s", path.c_str(), err != nullptr ? err : "unknown");
        lua_pop(this->m_impl->luaState, 1);
        return false;
    }
    return true;
}

bool LuaEngine::executeString(std::string_view code) {
    if (!this->isAvailable()) { return false; }

    std::string codeStr(code);
    int result = luaL_dostring(this->m_impl->luaState, codeStr.c_str());
    if (result != LUA_OK) {
        const char* err = lua_tostring(this->m_impl->luaState, -1);
        LOG_ERROR("Lua eval error: %s", err != nullptr ? err : "unknown");
        lua_pop(this->m_impl->luaState, 1);
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
