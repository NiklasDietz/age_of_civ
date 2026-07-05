# Component: data

## Responsibility

Loads game definitions from JSON files at startup and populates runtime vectors used by
the simulation. Provides compile-time constexpr fallbacks so the game runs even with
missing data files.

## Key files

- [include/aoc/data/DataLoader.hpp](../../../include/aoc/data/DataLoader.hpp) —
  `DataLoader`: reads JSON from `data/definitions/` and populates typed vectors:
  - `RuntimeBuildingDef` from `buildings.json`
  - `RuntimeUnitTypeDef` from `units.json`
  - `RuntimeTechDef` from `techs.json`
  - `RuntimeGoodDef` from `goods.json`
  - `RuntimeRecipeDef` from `recipes.json`
  - `RuntimeImprovementDef` from `improvements.json`
  - `LeaderPersonality` from `leaders.json`

  Falls back to hardcoded constexpr arrays in the corresponding simulation headers if a
  JSON file is missing or fails to parse.

## Data directory

`data/definitions/` — 7 JSON files:
`buildings.json`, `goods.json`, `improvements.json`, `leaders.json`, `recipes.json`,
`techs.json`, `units.json`.

`data/scripts/` — Lua scripts loaded by `LuaEngine` (init, events, victory conditions).

## Public surface

- `DataLoader::load(definitionsPath)` — called once by `Application` and
  `HeadlessSimulation` before `GameServer::initialize()`. Populates the runtime vectors
  accessible via `DataLoader::buildings()`, `units()`, `techs()`, etc.
- The loaded definitions are passed into simulation subsystems at game initialization
  (production system, unit type registry, tech tree).

## Internal structure

Single file pair (`DataLoader.hpp` / `src/data/DataLoader.cpp`). The JSON parsing uses
`SimpleYaml` from `core` for simple key-value extraction; there is no external JSON
library dependency.
