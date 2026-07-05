# Component: scripting

## Responsibility

Embeds a Lua scripting engine (LuaJIT preferred, Lua 5.4 fallback) for moddable game
logic: custom victory conditions, world events, AI personality overrides, building/unit
special effects, and map generation rules. Compiles to a no-op stub when neither LuaJIT
nor Lua is found.

## Key files

- [include/aoc/scripting/LuaEngine.hpp](../../../include/aoc/scripting/LuaEngine.hpp) —
  `LuaEngine`: PIMPL wrapper around `lua_State`. Key methods:
  - `initialize(scriptsPath)` — creates the Lua state and loads `data/scripts/init.lua`,
    then event and victory scripts.
  - `bindGameState(gameState, grid)` — exposes game state as read-only Lua tables.
  - `executeFile(path)` / `executeString(code)` — script loading.
  - `callFunction(funcName)` — event trigger and victory check dispatch.
  - `registerFunction(name, func)` — C++ → Lua function registration.

  When `AOC_HAS_LUA` is not defined, all methods are no-ops and `isAvailable()` returns
  false.

## Script loading order

1. `data/scripts/init.lua` — core setup and game API registration
2. `data/scripts/events/{name}.lua` — world event definitions
3. `data/scripts/victory/{name}.lua` — custom victory conditions
4. `data/mods/{modname}/init.lua` — per-mod scripts (when mod loading is active)

## Public surface

- `LuaEngine::initialize()` — called once by `GameServer::initialize()`.
- `LuaEngine::callFunction("onTurnEnd")` / `callFunctionWithPlayer(…)` /
  `callFunctionWithTurn(…)` — called from `TurnProcessor` at the world-events and
  victory-check phases.
- `LuaEngine::bindGameState()` — re-bound after save/load restores a new `GameState`.

## Internal structure

Single file pair (`LuaEngine.hpp` / `src/scripting/LuaEngine.cpp`). The `Impl` PIMPL
struct holds the raw `lua_State*` and the binding tables; it is conditionally compiled
via `#ifdef AOC_HAS_LUA`.
