# Component: balance / ml

## Responsibility

Exposes all tunable game constants as a flat float genome and provides a genetic
algorithm that runs embedded headless simulations in parallel to discover good parameter
values and leader personalities automatically.

## Key files

### `src/balance/` — runtime balance parameters

- [include/aoc/balance/BalanceParams.hpp:29](../../../include/aoc/balance/BalanceParams.hpp#L29)
  / `src/balance/BalanceParams.cpp` — `BalanceParams`: the struct of tunable floats and
  ints (loyalty, culture victory threshold, religion dominance, production chain
  multipliers, stockpile caps, …); `aoc::balance::params()` returns the process-global
  singleton. `BalanceGenome` ([:152](../../../include/aoc/balance/BalanceParams.hpp#L152))
  is the same values packed as a flat `std::array<float>` for GA operators, with
  `toParams()` / `fromParams()`.

### `ml/cpp/` — genetic algorithm tuner (separate CMake target `aoc_evolve`)

- `ml/cpp/GeneticAlgorithm.hpp` / `.cpp` — `Individual`
  ([:73](../../../ml/cpp/GeneticAlgorithm.hpp#L73)), `GAConfig`
  ([:124](../../../ml/cpp/GeneticAlgorithm.hpp#L124)), `ParamBounds`, `DifficultyTiers`;
  real-valued selection, crossover and Gaussian mutation over the genome vector.
- `ml/cpp/FitnessEvaluator.hpp` / `.cpp` — `runSimulation`
  ([ml/cpp/FitnessEvaluator.cpp:75](../../../ml/cpp/FitnessEvaluator.cpp#L75)) runs an
  **embedded** headless game linked from `aoc_lib` (it does not shell out to
  `aoc_simulate` nor use `GameServer`) and returns a `SimulationResult`
  ([:27](../../../ml/cpp/FitnessEvaluator.hpp#L27)).
- `ml/cpp/BalanceMetrics.hpp` — the per-run health metrics a result is scored on.
- `ml/cpp/BalanceTuner.hpp` / `.cpp` — `BalanceHealth`, `BalanceIndividual`,
  `BalanceGAConfig` ([:44-61](../../../ml/cpp/BalanceTuner.hpp#L44)) and the driver that
  initialises the population, runs generations, and writes the best genome to a plain-text
  summary (`evolved_balance.txt`, plus a paste-ready block on stderr).
- `ml/cpp/ThreadPool.hpp:28` — fixed-size thread pool running one embedded simulation per
  thread.
- `ml/cpp/main.cpp:420` — the `aoc_evolve` entry point.

## Public surface

- `aoc::balance::params()` — read by `TurnProcessor` and simulation sub-modules as a
  runtime override on top of the compile-time constants in `BalanceConfig.hpp`.
- `aoc_evolve` writes plain-text summaries (`evolved_balance.txt` for balance,
  `evolved_summary.txt` for AI leaders). Feedback into the game is manual: values are
  pasted into `BalanceParams` defaults / the `LEADER_PERSONALITIES` table, or loaded for
  a run through `simulation/ai/TunedLeaderIO`. `BalanceParams` loads no file at startup.

## Internal structure

`src/balance/` is a single file pair linked into `aoc_lib`. `ml/cpp/` has its own
`CMakeLists.txt` and builds separately as `aoc_evolve`. The GA operates purely through
embedded headless simulation, never touching rendering or UI; its include edges are
`simulation`, `map`, `game`, `core` and `balance`.

<!-- arch-doc: class-diagram=skipped; plain GA records plus the BalanceParams/BalanceGenome conversion pair -->
<!-- arch-doc: state-machines=none; no transitioned enum found -->
