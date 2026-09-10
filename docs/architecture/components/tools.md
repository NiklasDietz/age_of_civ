# Component: tools

## Responsibility

The four non-interactive executables built from `src/tools/`: the headless turn
simulator, the map generator CLI with its loopback inspector, the decision-log converter,
and the build-time font baker.

## Key files

- `src/tools/HeadlessSimulation.cpp` (built as `aoc_simulate`) — `main`
  ([:1044](../../../src/tools/HeadlessSimulation.cpp#L1044)) parses `--turns`, `--players`,
  `--seed`, `--output`, `--map-size`, `--map-cache`, `--trace-file`, `--victory-types`,
  `--placement`, `--log-level` or a YAML config, then `runHeadlessSimulation`
  ([:322](../../../src/tools/HeadlessSimulation.cpp#L322)) generates or loads the cached
  map ([:395-410](../../../src/tools/HeadlessSimulation.cpp#L395)), places starts
  ([:455](../../../src/tools/HeadlessSimulation.cpp#L455)), founds a city per player
  ([:475](../../../src/tools/HeadlessSimulation.cpp#L475)) and loops `processTurn`
  ([:685](../../../src/tools/HeadlessSimulation.cpp#L685)). Output: one CSV row per player
  per turn, a `_events.csv` from the `TurnEventLog`, a `_tiles.csv`, and an optional
  binary `DecisionLog`. Used by CI smoke tests, the golden and determinism gates, and
  `scripts/sim_health.py`.
- `src/tools/MapGenCli.cpp` (built as `aoc_mapgen`) — `main`
  ([:279](../../../src/tools/MapGenCli.cpp#L279)): seed, size, topology and physics flags;
  calls `MapGenerator::generate()` and exports the `HexGrid` as CSV/PNG. With the server
  flag it serves `GET /ping`, `/info`, `/plates`, `/tile` and `POST /dump/*`, `/sim/*`,
  `/quit` ([:749-965](../../../src/tools/MapGenCli.cpp#L749)) through `DebugServer` for the
  creator tooling and `tools/mapgen_metrics.py`.
- `src/tools/TraceDump.cpp` (built as `aoc_trace_dump`) — `main`
  ([:103](../../../src/tools/TraceDump.cpp#L103)) reads a binary `DecisionLog` and writes
  CSV or JSON via `readDecisionLog()` from `core`.
- `src/tools/FontBake.cpp` (built as `aoc_font_bake`) — `main`
  ([:84](../../../src/tools/FontBake.cpp#L84)) rasterises the bundled fonts with
  `stb_truetype` into the atlas blob described by `include/aoc/ui/FontAtlasFormat.hpp`; the
  only target that links the TrueType parser.

## Public surface

None: each file is a `main()` over `aoc_lib`. `ml/cpp/FitnessEvaluator.cpp` runs its own
embedded simulation (`runSimulation`, `ml/cpp/FitnessEvaluator.cpp:75`) rather than
shelling out to `aoc_simulate`.

## Internal structure

Four independent translation units. Include edges: `tools → core, debug, game, map, save,
simulation, ui` (the headless tool reads `GameSetupConfig` from `ui` for its setup types).

<!-- arch-doc: class-diagram=skipped; four main() translation units, no shared types -->
<!-- arch-doc: state-machines=none; no transitioned enum found -->
