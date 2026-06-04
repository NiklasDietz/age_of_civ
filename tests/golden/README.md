# Golden regression baselines

Deterministic before/after equivalence checks for refactors and performance
work. Baselines are **machine/toolchain-specific** for the simulation (float
results depend on the compiler and flags), so they are generated locally and
committed by the person doing the work, not shipped from CI.

## Layout

- `sim/` — `aoc_simulate` output (`seed<N>.csv`, `seed<N>_events.csv`,
  `seed<N>_tiles.csv`) for the scenarios in `scripts/regression_sim.sh`.
- `mapgen/` — `aoc_mapgen` per-tile CSV (`seed<N>.csv`) and plate stats
  (`seed<N>_plates.csv`) for the scenarios in `scripts/regression_mapgen.sh`.

## Workflow

1. Build the tools (release recommended for stable timings):
   ```
   cmake --preset release && cmake --build --preset release
   ```
2. On a clean tree, generate baselines once and commit them:
   ```
   scripts/regression_sim.sh --update
   scripts/regression_mapgen.sh --update
   git add tests/golden && git commit -m "test: golden regression baselines"
   ```
3. After each change, verify equivalence:
   ```
   scripts/regression_sim.sh        # exact for int/enum/tile cols, tol for floats
   scripts/regression_mapgen.sh     # exact (map gen is fully deterministic)
   ```
   or via ctest: `ctest --preset release -R regression`.

## When to regenerate baselines

Only when a change **intentionally** alters output: e.g. the WP-11 polar-erosion
fix changes generated maps, and ARCH-5b (initialising `GamePace`) changes sim
output for non-default game lengths. Regenerate, eyeball the diff to confirm it
matches the intended change, then commit the new baselines in the same change.

Determinism requirements: `OMP_NUM_THREADS=1` (the scripts set this), a fixed
`--seed`, and a single build/toolchain. The sim's `xoshiro256**` RNG and the
single-threaded map-gen path are otherwise fully reproducible.
