# Component: net

## Responsibility

Defines the server/client game architecture and the transport layer that connects them.
`GameServer` is the single authority for game state; `GameClient` sends player commands.
`LocalTransport` connects them in-process (single player, zero overhead). Future
multiplayer replaces `LocalTransport` with a network adapter without changing the
server or client.

## Key files

- [include/aoc/net/GameServer.hpp](../../../include/aoc/net/GameServer.hpp) —
  `GameServer`: owns `GameState`, `HexGrid`, `EconomySimulation`, `DiplomacyManager`,
  `TurnManager`, `Random`, and the `AIController` vector. `tick()` consumes pending
  commands from `ITransport`, validates them, and — when all human players have sent
  `EndTurn` — calls `TurnProcessor::processTurn()`, then broadcasts per-player
  `GameStateSnapshot`s respecting fog of war.
- [include/aoc/net/GameClient.hpp](../../../include/aoc/net/GameClient.hpp) —
  `GameClient`: thin command sender and snapshot receiver. Provides typed `sendCommand()`
  helpers (`moveUnit`, `foundCity`, `setProduction`, `setResearch`, …). Polls
  `ITransport` for `StateUpdate`s (real-time per-action) and `GameStateSnapshot`s
  (end-of-turn full state).
- [include/aoc/net/Transport.hpp](../../../include/aoc/net/Transport.hpp) —
  `ITransport` abstract interface (3 channels: commands → server, state updates → all
  clients, snapshots → specific client). `LocalTransport` implements all channels with
  unsynchronised in-process vectors; asserts single-thread ownership in debug builds.
- [include/aoc/net/NetInterface.hpp](../../../include/aoc/net/NetInterface.hpp) —
  `NetInterface` (older abstract interface, kept alongside `ITransport`); `NetworkMode`
  enum: `LocalOnly`, `LAN`, `Online`.
- [include/aoc/net/CommandBuffer.hpp](../../../include/aoc/net/CommandBuffer.hpp) —
  `GameCommand` variant and typed command structs: `EndTurnCommand`, `MoveUnitCommand`,
  `AttackUnitCommand`, `FoundCityCommand`, `SetProductionCommand`, `SetResearchCommand`,
  `SetTaxRateCommand`.
- [include/aoc/net/StateUpdate.hpp](../../../include/aoc/net/StateUpdate.hpp) —
  `StateUpdate`: small delta message broadcast to all clients immediately after each
  command executes (unit moved, combat result, etc.).
- [include/aoc/net/GameStateSnapshot.hpp](../../../include/aoc/net/GameStateSnapshot.hpp)
  — `GameStateSnapshot`: per-player full state view sent after the turn simulation tick,
  filtered by fog of war.
- `src/net/GameDBus.cpp` / [include/aoc/net/GameDBus.hpp](../../../include/aoc/net/GameDBus.hpp)
  — D-Bus IPC for Linux desktop integration (taskbar progress bar, Unity launcher rich
  presence). Compiled only when sdbus-cpp is detected by CMake.

## Public surface

- `GameServer::initialize(config)` / `tick()` — called by `Application` (interactive)
  and `HeadlessSimulation` (headless sim).
- `GameServer::grid()` / `economy()` — read by `Application` for UI queries.
- `GameClient::sendCommand(…)` / `pollUpdates()` / `pollSnapshot()` — used by
  `Application` to send the human player's actions and receive turn results.
- `LocalTransport` — wired between `GameServer` and `GameClient` by `Application`.

## Internal structure

Flat directory. The architecture deliberately mirrors a client-server split even in
single player: `Application` instantiates both `GameServer` and `GameClient`, connects
them through `LocalTransport`, and drives the loop. This ensures the code path used for
single player is identical to the code path a future multiplayer server would use.
