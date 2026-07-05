# Component: mod

## Responsibility

Placeholder loader for mod JSON definitions (unit types, buildings, techs, civilizations).
All methods currently log a warning and return false; the system is scaffolded but not yet
implemented.

## Key files

- `src/mod/ModLoader.cpp` /
  [include/aoc/mod/ModLoader.hpp](../../../include/aoc/mod/ModLoader.hpp) — `ModLoader`:
  four methods (`loadUnitDefs`, `loadBuildingDefs`, `loadTechDefs`, `loadCivDefs`) each
  accepting a filepath. Each logs `"JSON parsing not yet implemented"` and returns false.

Lua-based mod scripting is handled by the **scripting** subsystem; `LuaEngine` loads
`data/mods/{modname}/init.lua` when mod loading is active.

## Public surface

- `ModLoader::loadUnitDefs(path)` etc. — intended to be called from `Application` after
  `DataLoader::load()`, to overlay mod definitions on top of the base game data.

## Internal structure

Single file pair. The actual implementation is deferred pending a decision on which JSON
library to add (see `DEPENDENCIES.txt` — there is currently no JSON parser dependency
beyond `SimpleYaml` from `core`).
