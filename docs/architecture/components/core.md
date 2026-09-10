# Component: core

## Responsibility

Provides the foundational types, utilities, and services shared by every other subsystem:
entity identifiers, domain type aliases, a deterministic PRNG, structured logging, a
compact binary AI decision log, JSON escaping, order-independent reductions, and a
handful of RAII helpers.

## Key files

- [include/aoc/core/Types.hpp](../../../include/aoc/core/Types.hpp) — `EntityId` (20-bit
  index + 12-bit generation), `PlayerId`, `TurnNumber`, `StrongId<Tag>` for all 16-bit
  domain IDs, `NULL_ENTITY` sentinel.
- [include/aoc/core/Random.hpp](../../../include/aoc/core/Random.hpp) — `aoc::Random`:
  xoshiro256\*\* seeded via SplitMix64, with `fork()` for independent sub-streams.
- [include/aoc/core/Log.hpp](../../../include/aoc/core/Log.hpp) — `LOG_DEBUG/INFO/WARN/
  ERROR/FATAL` macros. Single `fprintf` per call; runtime severity gate
  (`aoc::log::g_minSeverity`); DEBUG stripped in NDEBUG builds.
- [include/aoc/core/DecisionLog.hpp](../../../include/aoc/core/DecisionLog.hpp) —
  `DecisionLog`: compact binary log of AI production/research decisions and per-turn
  summaries; reader via `readDecisionLog()`; a thread-local `currentDecisionLog()` pointer
  lets AI call sites reach it without threading a pointer everywhere.
- [include/aoc/core/ErrorCodes.hpp](../../../include/aoc/core/ErrorCodes.hpp) — `ErrorCode`
  enum used as return values by the save/load path and the request layer.
- [include/aoc/core/PathGuard.hpp](../../../include/aoc/core/PathGuard.hpp) —
  `PathGuard`: validates and normalizes a filesystem path on construction.
- [include/aoc/core/SimpleYaml.hpp](../../../include/aoc/core/SimpleYaml.hpp) — minimal
  YAML reader used for the headless configuration files.
- [include/aoc/core/JsonUtil.hpp](../../../include/aoc/core/JsonUtil.hpp) — escaping of
  `"`, `\` and control characters for strings embedded in the debug server's JSON.
- [include/aoc/core/Deterministic.hpp](../../../include/aoc/core/Deterministic.hpp) —
  order-independent reductions over hash-ordered associative containers, so iteration
  order cannot leak into the simulation.
- [include/aoc/core/Selectable.hpp](../../../include/aoc/core/Selectable.hpp) — the
  interface for selectable map objects (units, cities, tiles); includes `map/HexCoord.hpp`,
  which is the one `core → map` edge.

## Public surface

Every subsystem depends on `core`. The types used most widely:

- `aoc::EntityId`, `aoc::PlayerId`, `aoc::TurnNumber` — entity, player and turn indexing
- `aoc::ResourceId`, `aoc::TechId`, `aoc::BuildingId`, … (all `StrongId<Tag>` specializations)
- `aoc::Random` — the game PRNG; instances live in `Application` and `HeadlessSimulation`
  and are saved alongside game state for determinism
- `LOG_*` macros — used at every severity level across all subsystems
- `aoc::core::DecisionLog` / `currentDecisionLog()` — AI trace capture from `TurnProcessor`
  and the AI controllers

## Internal structure

`core` has no sub-directories. The ten headers are independent of one another (except
`DecisionLog.hpp` → `ErrorCodes.hpp`); five have sources under `src/core/`.

<!-- arch-doc: class-diagram=skipped; independent utility headers, no interrelated core types -->
<!-- arch-doc: state-machines=none; no transitioned enum found -->
