# Component: debug

## Responsibility

Provides a localhost HTTP debug server for live game-state inspection during development,
and hosts the three tool entry points (`HeadlessSimulation`, `TraceDump`, `MapGenCli`)
that are built as separate executables.

## Key files

- `src/debug/DebugServer.cpp` (no corresponding public header in `include/aoc/`) —
  `DebugServer`: wraps `cpp-httplib`'s `httplib::Server`. Binds to `127.0.0.1` only.
  Uses a pre-routing `Host`-header allowlist so the server cannot be reached by
  DNS-rebinding attacks even though it runs on loopback. Activated at runtime via the
  `--enable-debug-server` flag. Thread-pool size set to 4 via
  `CPPHTTPLIB_THREAD_POOL_COUNT`. All response JSON strings pass through
  `escapeJsonString()` to prevent injected quotes or control characters.

- `src/tools/HeadlessSimulation.cpp` (built as `aoc_simulate`) — Runs a full game
  simulation without any display. Accepts `--turns N`, `--players N`, `--seed S`,
  `--output path.csv`. Calls `GameServer::initialize()` and loops
  `GameServer::tick()`. Used for CI smoke tests and GA fitness evaluation.

- `src/tools/TraceDump.cpp` (built as `aoc_trace_dump`) — Reads a binary
  `DecisionLog` file (written by `aoc_simulate --decision-log path`) and converts it
  to CSV or JSON. Uses `readDecisionLog()` from `core::DecisionLog`.

- `src/tools/MapGenCli.cpp` (built as `aoc_mapgen`) — Standalone map generator CLI.
  Accepts seed, size, and topology flags; calls `MapGenerator::generate()` and exports
  the resulting `HexGrid` as a PNG/CSV for inspection. Useful during worldgen tuning.

## Public surface

- `DebugServer::start(port)` / `stop()` / `route(method, path, handler)` / `routeJson(…)`
  — called from `Application` when `--enable-debug-server` is active. Routes registered
  by each subsystem expose read-only snapshots (game state, economy, AI decisions).

## Internal structure

`src/debug/` holds only `DebugServer.cpp`. The three tool entry points live in
`src/tools/`; each is a thin `main()` that initializes the library and drives the
relevant subsystem. No headers in `include/aoc/debug/` are currently needed by other
subsystems (the debug server is only accessed through its own `DebugServer.hpp`).
