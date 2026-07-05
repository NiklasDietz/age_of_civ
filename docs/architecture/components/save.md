# Component: save

## Responsibility

Serializes and deserializes the complete game state to a versioned binary file format,
with forward-compatible section skipping and a migration chain from v1 to the current
format.

## Key files

- [include/aoc/save/Serializer.hpp](../../../include/aoc/save/Serializer.hpp) —
  `saveGame()` / `loadGame()`: free functions that write/read the entire game. Format:
  `[Header: magic "AOC\0" + version(4) + flags(4) + dataSize(4)]` followed by
  self-describing sections (`sectionId(2) + sectionSize(4) + data`). Current version:
  `SAVE_VERSION = 10`. Currently 38 named `SectionId` values covering map grid,
  entities, diplomacy, market, fog of war, PRNG state, per-player tech/civic/monetary/
  government/production/religion/tourism/space-race/prestige state, and global
  wonder/barbarian/city-state trackers.

  `ReadBuffer` is bounds-checked with a sticky `m_corrupt` flag — once any read
  trips an underflow, all subsequent reads are no-ops returning zero/empty. This
  blocks hostile save-file OOB reads. `ReadBuffer::canReadRecords(count, minBytes)`
  guards large-reserve loops before allocation.

- [include/aoc/save/SaveVersioning.hpp](../../../include/aoc/save/SaveVersioning.hpp)
  — Migration chain: `CURRENT_SAVE_VERSION` constant and per-version `migrate()`
  functions (v1→v2→…→v10). Each step defaults missing sections. Unknown sections
  (from newer versions) are skipped via their stored `sectionSize`.

## Public surface

- `saveGame(filepath, gameState, grid, turnManager, economy, diplomacy, fogOfWar, rng)`
  — returns `ErrorCode::Ok` or `ErrorCode::SaveFailed`.
- `loadGame(filepath, gameState, grid, turnManager, economy, diplomacy, fogOfWar, rng)`
  — returns `ErrorCode::Ok`, `LoadFailed`, `SaveVersionMismatch`, or `SaveCorrupted`.

Both functions are called from `Application` (interactive save/load) and will be called
from `aoc_simulate` once checkpoint support is added.

## Internal structure

Two files: `Serializer` (format + read/write primitives) and `SaveVersioning`
(migration). The save path calls into every simulation subsystem to serialize its
state; the load path reconstructs those subsystems in the same order. Section IDs are
stable — they are never renumbered, only extended.
