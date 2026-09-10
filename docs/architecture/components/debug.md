# Component: debug

## Responsibility

Provides the loopback HTTP debug server and the typed command and snapshot vocabulary
behind it: the game-control API used by tests and by the MCP wrapper, and the UI-control
API that drives the widget tree from outside the process.

## Key files

- [include/aoc/debug/DebugServer.hpp:68](../../../include/aoc/debug/DebugServer.hpp#L68) /
  `src/debug/DebugServer.cpp` — `DebugServer`: wraps `cpp-httplib`'s `httplib::Server`.
  Binds to `127.0.0.1` only, with a pre-routing `Host`-header allowlist so it cannot be
  reached by DNS rebinding. `routeJson(Method, path, handler)`
  (`Method` at [:73](../../../include/aoc/debug/DebugServer.hpp#L73)) registers a route;
  every response string passes through JSON escaping. Off by default; enabled with
  `--enable-debug-server`, port 9876.
- [include/aoc/debug/GameControlCommand.hpp](../../../include/aoc/debug/GameControlCommand.hpp)
  — the typed command structs (`MoveUnitCommand` [:21](../../../include/aoc/debug/GameControlCommand.hpp#L21),
  `AttackUnitCommand`, `FoundCityCommand`, `SetProductionCommand`, `SetResearchCommand`,
  `EndTurnCommand`, …) and the `GameControlCommand` variant
  ([:321](../../../include/aoc/debug/GameControlCommand.hpp#L321)) the server enqueues for
  the main thread.
- [include/aoc/debug/GameControlValidation.hpp](../../../include/aoc/debug/GameControlValidation.hpp)
  / `src/debug/GameControlValidation.cpp` — validates a command against the live
  `GameState` before it is queued (player, unit, city and range checks), so a bad request
  is refused on the request thread.
- [include/aoc/debug/GameSnapshot.hpp:58](../../../include/aoc/debug/GameSnapshot.hpp#L58)
  / `src/debug/GameSnapshot.cpp` — `GameSnapshot`: the per-player JSON view (treasury,
  cities, units, research, diplomacy) served by `GET /game/state` and friends.
- [include/aoc/debug/UiControlCommand.hpp](../../../include/aoc/debug/UiControlCommand.hpp)
  — `ClickWidgetCommand`, `ClickAtCommand`, `ScrollAtCommand`, `TakeScreenshotCommand` and
  their variant ([:45](../../../include/aoc/debug/UiControlCommand.hpp#L45)), behind
  `GET /ui/tree`, `POST /ui/click`, `/ui/click-at`, `/ui/scroll`, `/debug/screenshot`.

## Public surface

- `DebugServer::start()` / `stop()` / `routeJson(...)` — `Application` creates the server
  ([src/app/Application.cpp:582](../../../src/app/Application.cpp#L582)) and registers 62
  routes (`src/app/Application.cpp:602` onward, `src/app/Application_CityControl.cpp:81`
  onward); `aoc_mapgen` registers its own inspector routes
  (`src/tools/MapGenCli.cpp:749-965`). The full route inventory is in
  [interactions.md](../interactions.md#entry-points).
- `tools/mcp_server.py` wraps the routes as 60 `aoc_*` MCP tools for an LLM client;
  `tools/mcp_e2e_test.py` exercises them end to end.

## Internal structure

`src/debug/` holds the server and the two value-type translation units; the routes
themselves are registered by the consumers (`app`, `tools`). Commands never mutate game
state on the request thread: they are validated, queued, and drained by `Application`.

<!-- arch-doc: class-diagram=skipped; one server class plus independent command and snapshot value types -->
<!-- arch-doc: state-machines=none; no transitioned enum found -->
