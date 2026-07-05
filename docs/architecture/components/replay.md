# Component: replay

## Responsibility

Records per-turn per-player game snapshots for post-game analysis and replay viewing.
Each frame captures military unit count, territory (city count), population, techs
researched, and a composite score.

## Key files

- `src/replay/ReplayRecorder.cpp` /
  [include/aoc/replay/ReplayRecorder.hpp](../../../include/aoc/replay/ReplayRecorder.hpp)
  — `ReplayRecorder`: holds a `std::vector<ReplayFrame>`. `recordFrame(gameState, turn)`
  iterates all major players and records a `ReplayFrame::PlayerSnapshot` per player.
  Score formula: `military * 5 + territory * 20 + population * 2 + techs * 10`.

## Public surface

- `ReplayRecorder::recordFrame(gameState, turn)` — called from `TurnProcessor` at the
  end of each turn when a recorder is attached to `TurnContext`.
- `ReplayRecorder::frames()` — read by the score screen and future replay viewer.

## Internal structure

Single file pair. The recorder holds frames in memory; serialization to disk is not yet
implemented. Players with zero military and zero territory are skipped (inactive/
eliminated players).
