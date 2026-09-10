# Component: save

## Responsibility

Serializes and deserializes the complete game state to a versioned binary file format,
written atomically and loaded only on an exact version match; also owns the `.aocmap`
map-cache format and save-slot naming.

## Key files

- [include/aoc/save/Serializer.hpp](../../../include/aoc/save/Serializer.hpp) —
  `saveGame()` / `loadGame()`: free functions that write/read the entire game. Format:
  `[Header: magic "AOC\0" + version(4) + flags(4) + dataSize(4)]` followed by
  self-describing sections (`sectionId(2) + sectionSize(4) + data`). 44 named `SectionId`
  values; `saveGame` calls 41 section writers. Current version:
  `CURRENT_SAVE_VERSION = 34` (v34: `ActiveDeals` carries the deals in force,
  `MonetaryState` carries the private money pools and coinage metal, each unit record
  carries its `TraderComponent`, and the dead `PlayerEconomyComponent::treasury` is gone).

  `ReadBuffer` is bounds-checked with a sticky corrupt flag — once any read trips an
  underflow, all subsequent reads are no-ops returning zero/empty. `canReadRecords(count,
  minBytes)` guards large-reserve loops before allocation, and every enum read is
  range-checked. Writes go to `path.tmp`, are fsynced, renamed over the target, and the
  directory is fsynced ([src/save/Serializer.cpp:2191-2267](../../../src/save/Serializer.cpp#L2191)).

- [include/aoc/save/SaveVersioning.hpp](../../../include/aoc/save/SaveVersioning.hpp)
  — `CURRENT_SAVE_VERSION`, the per-version changelog, and the no-migration policy: a
  file loads only when its header version equals the current one
  ([src/save/Serializer.cpp:2329-2337](../../../src/save/Serializer.cpp#L2329)). Unknown
  sections are skipped via their stored `sectionSize`; a known section that decodes to a
  different length is rejected as `SaveCorrupted`.
- [include/aoc/save/MapFile.hpp](../../../include/aoc/save/MapFile.hpp) — the `.aocmap`
  cache written and read by the headless simulator (`saveMapFile`
  [:62](../../../include/aoc/save/MapFile.hpp#L62), `loadMapFile`
  [:66](../../../include/aoc/save/MapFile.hpp#L66)); `isGameGridLayer`
  ([:44](../../../include/aoc/save/MapFile.hpp#L44)) names the grid layers a game save
  carries versus the worldgen-only layers that stay in the cache.
- [include/aoc/save/SaveSlots.hpp](../../../include/aoc/save/SaveSlots.hpp) — numbered
  save-slot naming shared by every load/save UI path and `QUICKSAVE_FILENAME`
  ([:17](../../../include/aoc/save/SaveSlots.hpp#L17)).

## Public surface

- `saveGame(filepath, gameState, grid, turnManager, economy, diplomacy, fogOfWar, rng)`
  — returns `ErrorCode::Ok` or `ErrorCode::SaveFailed`.
- `loadGame(filepath, gameState, grid, turnManager, economy, diplomacy, fogOfWar, rng)`
  — returns `ErrorCode::Ok`, `LoadFailed`, `SaveVersionMismatch`, or `SaveCorrupted`.
- `saveMapFile` / `loadMapFile` — the headless map cache.

Callers: `Application` (manual save/load, slots, quicksave and spectator restore:
`src/app/Application.cpp:4389`, `:4424`, `:4566`, `:4617`, `:4632`, `:5366`),
`HeadlessSimulation` (map cache), the `LoadGameMenu` screen (slot listing).

## Internal structure

Two source files: `Serializer.cpp` (format, read/write primitives, one writer and one
reader case per section) and `MapFile.cpp`. The save path calls into every simulation
subsystem to serialize its state; the load path reconstructs those subsystems in section
order (Entities before the sections that reference cities and units). Section IDs are
stable — never renumbered, only extended. The known-good corpus under
`tests/data/saves/` is regenerated on every version bump.

<!-- arch-doc: class-diagram=skipped; two buffer types plus free functions, not interrelated -->
<!-- arch-doc: state-machines=none; no transitioned enum found -->
