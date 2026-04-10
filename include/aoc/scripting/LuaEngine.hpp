#pragma once

/**
 * @file LuaEngine.hpp
 * @brief LuaJIT scripting engine for moddable game logic.
 *
 * Provides a Lua interpreter for:
 *   - Victory conditions (custom win logic)
 *   - World events (scripted triggers and effects)
 *   - AI personality overrides (modders tweak leader behavior)
 *   - Building/unit special effects (abilities as Lua callbacks)
 *   - Map generation rules (custom placement)
 *
 * The engine exposes game state to Lua as read-only tables and provides
 * a set of action functions that scripts can call to modify the game.
 *
 * Optional dependency: only compiles if Lua 5.4+ is found by CMake.
 * When Lua is not available, the engine is a no-op stub.
 *
 * Script loading order:
 *   1. data/scripts/init.lua (core setup, API registration)
 *   2. data/scripts/events/{name}.lua (world event definitions)
 *   3. data/scripts/victory/{name}.lua (custom victory conditions)
 *   4. data/mods/{modname}/init.lua (per-mod scripts)
 */

#include "aoc/core/Types.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace aoc::ecs { class World; }
namespace aoc::map { class HexGrid; }

namespace aoc::scripting {

/**
 * @brief Lua scripting engine wrapper.
 *
 * When Lua is available (AOC_HAS_LUA defined), this wraps a lua_State
 * and provides safe bindings to game systems. When Lua is not available,
 * all methods are no-ops.
 */
class LuaEngine {
public:
    LuaEngine();
    ~LuaEngine();

    LuaEngine(const LuaEngine&) = delete;
    LuaEngine& operator=(const LuaEngine&) = delete;
    LuaEngine(LuaEngine&&) noexcept;
    LuaEngine& operator=(LuaEngine&&) noexcept;

    /// Initialize the Lua state and load core scripts.
    /// @return true if Lua is available and initialized.
    bool initialize(const std::string& scriptsPath);

    /// Whether Lua scripting is available and initialized.
    [[nodiscard]] bool isAvailable() const;

    /// Execute a Lua script file.
    /// @return true if the script executed without errors.
    bool executeFile(const std::string& path);

    /// Execute a Lua string.
    bool executeString(std::string_view code);

    /// Bind game state for read access from scripts.
    void bindGameState(aoc::ecs::World& world, aoc::map::HexGrid& grid);

    /// Call a named Lua function (for event triggers, victory checks).
    /// @return true if the function returned truthy value.
    bool callFunction(std::string_view funcName);

    /// Call a Lua function with player ID argument.
    bool callFunctionWithPlayer(std::string_view funcName, PlayerId player);

    /// Call a Lua function with turn number argument.
    bool callFunctionWithTurn(std::string_view funcName, int32_t turn);

    /// Register a C++ function callable from Lua.
    /// @param name  Lua-visible function name.
    /// @param func  C++ function pointer.
    void registerFunction(std::string_view name, int32_t (*func)(void*));

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

} // namespace aoc::scripting
