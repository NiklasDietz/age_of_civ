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

    luaL_openlibs(this->m_impl->luaState);

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
