# Component: game

## Responsibility

Owns the runtime entity graph that all other subsystems read and write: the top-level
`GameState` container, `Player`, `City`, and `Unit` objects. This is the single source of
truth for "what is currently happening in the game."

## Key files

- [include/aoc/game/GameState.hpp](../../../include/aoc/game/GameState.hpp) — `GameState`:
  the root container. Owns `std::vector<unique_ptr<Player>>` for major players and a
  separate vector for city-state player slots (base ID 200+). Also holds global
  singletons: `GlobalClimateComponent`, `GlobalMonopolyComponent`, `GlobalSanctionTracker`,
  `GlobalWonderTracker`, `WorldCongressComponent`, `GlobalReligionTracker`,
  `VisibilityEventBus`; plus global collections: trade routes, commodity hoards, barbarian
  clans, city states, electricity agreements, and per-tile encampment supply buffers.
  Provides `recordTileEvent()` for the lightweight per-tile event stream.
- [include/aoc/game/Player.hpp](../../../include/aoc/game/Player.hpp) — `Player`: owns
  its cities (`vector<unique_ptr<City>>`), units, research state, economy, government,
  religion components, military unit count, and the `isHuman` flag.
- [include/aoc/game/City.hpp](../../../include/aoc/game/City.hpp) — `City`: owns
  production queue, citizens, buildings, district slots, stockpiles, and loyalty state.
- [include/aoc/game/Unit.hpp](../../../include/aoc/game/Unit.hpp) — `Unit`: movement
  points, combat strength, promotion tree, automation flag.
- [include/aoc/game/FogOfWar.hpp](../../../include/aoc/game/FogOfWar.hpp) — `FogOfWar`:
  per-player tile visibility bitsets; null in headless mode (fogOfWar pointer is
  optional in `TurnContext`).

## Public surface

`GameState` is passed by reference to nearly every simulation function and to the server
snapshot generator. Key access patterns used externally:

- `gameState.player(PlayerId)` — look up a player by ID (major or city-state slot)
- `gameState.humanPlayer()` / `setHumanPlayerId()` — which player the UI follows
- `gameState.players()` — iterate all major players (turn loop, victory check)
- `gameState.climate()`, `.worldCongress()`, `.religionTracker()` — global singletons
- `gameState.tradeRoutes()`, `.barbarianClans()`, `.cityStates()` — global collections
- `gameState.recordTileEvent()` — called from city founding, improvement, resource
  discovery paths for the tile-event stream

## Internal structure

Four entity types plus `FogOfWar`. Each entity type has one header and one source.
`GameState` aggregates all global state that has no natural owner among the player objects —
the "table" rather than any single chair. There is no ECS; the previous ECS `World` is kept
for backward-compatibility migration noted in the `GameState.hpp` doc comment, but new code
works through `GameState` directly.
