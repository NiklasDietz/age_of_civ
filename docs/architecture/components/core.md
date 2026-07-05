# Component: core

## Responsibility

Provides the foundational types, utilities, and services shared by every other subsystem:
entity identifiers, domain type aliases, a deterministic PRNG, structured logging, a
compact binary AI decision log, and a handful of RAII helpers.

## Key files

- [include/aoc/core/Types.hpp](../../../include/aoc/core/Types.hpp) — `EntityId` (20-bit
  index + 12-bit generation), `PlayerId`, `TurnNumber`, `StrongId<Tag>` for all 16-bit
  domain IDs, `NULL_ENTITY` sentinel.
- [include/aoc/core/Random.hpp](../../../include/aoc/core/Random.hpp) — `aoc::Random`:
  xoshiro256\*\* seeded via SplitMix64. `constexpr` throughout so map generation can run
  at compile time if needed.
- [include/aoc/core/Log.hpp](../../../include/aoc/core/Log.hpp) — `LOG_DEBUG/INFO/WARN/
  ERROR/FATAL` macros. Single `fprintf` per call (thread-safe via stdio flockfile).
  Runtime severity gate (`aoc::log::g_minSeverity`); DEBUG stripped in NDEBUG builds.
- [include/aoc/core/DecisionLog.hpp](../../../include/aoc/core/DecisionLog.hpp) —
  `DecisionLog`: compact binary log of AI production/research decisions and per-turn
  summaries. Magic `"AOCL"`, version 1, ~20 bytes/record. Reader via `readDecisionLog()`
  visitor API; also exposed through a thread-local `currentDecisionLog()` pointer so AI
  call sites reach it without threading a pointer everywhere.
- [include/aoc/core/ErrorCodes.hpp](../../../include/aoc/core/ErrorCodes.hpp) — `ErrorCode`
  enum used as return values throughout the save/load path.
- [include/aoc/core/PathGuard.hpp](../../../include/aoc/core/PathGuard.hpp) —
  `PathGuard`: RAII guard that validates and normalizes a filesystem path on construction.
- [include/aoc/core/SimpleYaml.hpp](../../../include/aoc/core/SimpleYaml.hpp) — minimal
  YAML reader used for configuration files.

## Public surface

Every subsystem depends on `core`. The types used most widely:

- `aoc::EntityId` — all runtime entity references (units, cities, etc.)
- `aoc::PlayerId`, `aoc::TurnNumber` — turn and player indexing
- `aoc::ResourceId`, `aoc::TechId`, `aoc::BuildingId`, …  (all `StrongId<Tag>` specializations)
- `aoc::Random` — game PRNG; instance lives in `GameServer`/`HeadlessSimulation`, saved
  alongside game state for determinism
- `LOG_*` macros — used at every severity level across all subsystems
- `aoc::core::DecisionLog` / `ScopedDecisionLog` / `currentDecisionLog()` — AI trace
  capture, used from `TurnProcessor` and AI controllers

## Internal structure

`core` has no sub-directories. All seven headers are independent; there are no include
dependencies between them (except `DecisionLog.hpp` → `ErrorCodes.hpp`). The corresponding
sources mirror the header names under `src/core/`.
