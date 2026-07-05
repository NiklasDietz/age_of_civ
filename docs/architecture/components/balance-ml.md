# Component: balance / ml

## Responsibility

Exposes all tunable game constants as a flat float genome and provides a genetic
algorithm that runs headless simulations in parallel to discover good parameter values
automatically.

## Key files

### `src/balance/` — runtime balance parameters

- `src/balance/BalanceParams.cpp` /
  [include/aoc/balance/BalanceParams.hpp](../../../include/aoc/balance/BalanceParams.hpp)
  — `BalanceParams`: struct of ~13 floats/ints (loyalty, culture victory threshold,
  religion dominance, production chain multipliers, …). `aoc::balance::params()` returns
  the process-global singleton. `BalanceGenome`: the same values packed as a flat
  `std::array<float>` for GA operators; `toParams()` / `fromParams()` convert between
  representations.

### `ml/cpp/` — genetic algorithm tuner (separate CMake target)

- `ml/cpp/GeneticAlgorithm.cpp` / `.hpp` — standard real-valued GA: selection,
  crossover, Gaussian mutation over the genome vector.
- `ml/cpp/FitnessEvaluator.cpp` / `.hpp` — runs `aoc_simulate` (or calls `GameServer`
  directly) for N turns, extracts a fitness score (win-rate balance, score variance
  across AI players).
- `ml/cpp/BalanceTuner.cpp` / `.hpp` — top-level driver: initializes population,
  runs generations, writes best genome to a JSON file loaded by `BalanceParams` at
  startup.
- `ml/cpp/ThreadPool.hpp` — simple fixed-size thread pool used to run fitness
  evaluations in parallel (one headless sim per thread).

## Public surface

- `aoc::balance::params()` — read by `TurnProcessor` and simulation sub-modules as a
  runtime override on top of the compile-time constants in `BalanceConfig.hpp`.
- The ML tuner is a standalone binary (`aoc_evolve` when built); it writes a JSON params
  file that `BalanceParams` loads at next game startup.

## Internal structure

`src/balance/` is a single file pair linked into `aoc_lib`. `ml/cpp/` has its own
`CMakeLists.txt` and builds separately as `aoc_evolve` (not part of the main game
library). The GA operates purely through headless simulation, never touching rendering
or UI.
