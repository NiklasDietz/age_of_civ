# Phase 1 Findings

Generated from 40-sim narrow sweep (players 4,8 × turns 1500 × maps continents/islands/pangaea/archipelago × seeds 1-5). Binary built with `AOC_DIAG_IR=ON`. 6493 diagnostic `IR_BLOCKED` lines parsed across 240 civ-instances. Raw report in `PHASE1_DIAGNOSIS.md`, raw rows in `blocker_table.csv`.

## Headline

| Metric | This sweep | Memory baseline (2026-05-03) | Delta |
|---|---:|---:|---:|
| Civs reaching IR#1 | 35 / 240 (15%) | "near-saturation" | **REGRESSED** |
| Civs reaching IR#2 | 14 / 240 (6%) | 54-75 / ~288 (~22%) | **REGRESSED** |
| Civs reaching IR#3 | 0 / 240 (0%) | 1-5 firings / run | **REGRESSED** |
| Civs reaching IR#4 | 0 / 240 (0%) | 1-2 firings / run | **REGRESSED** |
| Civs reaching IR#5 | 0 / 240 (0%) | ~1-in-3 runs | **REGRESSED** |

This sweep is significantly **worse** than the state described in memory. Recent commit log shows 10 consecutive "balance fix" commits (`ae68bb2 .. 4366a69`). One of those plausibly broke the IR#1 chain — every downstream IR is gated on IR#1, so a single Charcoal-supply break would cascade.

## Dominant blockers (per IR level, from diagnostic logging)

### IR#1 (Steam Age) — 205 civs blocked
- **98.0%** missing **Charcoal good (id 79)**
- 2.0% missing tech 18 (SurfacePlate)

Recipe 38 (Wood -> Charcoal) is supposed to be near-universal. The fact that 98% of stuck civs cannot accumulate even one unit of Charcoal in any city stockpile (and `econ.totalSupply` shows zero ever) points to one of:
- recipe 38 not firing because Wood is not produced (forest-tile recipe issue)
- recipe 38 producing Charcoal that is consumed faster than logged (but `totalSupply` would still show non-zero)
- a recent commit disabled or re-gated the recipe

### IR#2 (Electric Age) — 226 civ-cases blocked
- **57.5% tech-blocked** -- top: Steel tech (id 47, 115 cases), Electricity (id 14, 8), Precision (id 22, 7)
- **42.5% goods-blocked** -- top: Oil (id 3, 59 cases), Steel good (id 64, 37)

Steel-tech reach is the single largest IR#2 gate. Of civs that DO have Steel tech, the next gate is Oil (the H1 hypothesis from the plan).

### IR#3 (Digital Age) — 240 civ-cases blocked
- **96.7% missing Semiconductors good (id 75)**
- 3.3% missing Semiconductors tech (id 23)

Cascade from IR#2: nobody reaches the chain.

### IR#4 -- 90% missing Software good. IR#5 -- 100% missing Fusion tech (expected at 1500-turn cap).

## Geography

| Map | %IR1 | %IR2 |
|---|---:|---:|
| archipelago | 2% | 2% |
| pangaea | 8% | 2% |
| continents | 22% | 7% |
| islands | 27% | 13% |

Archipelago is the worst case (consistent with the H4 placement-gap hypothesis), but even continents at 22% is far below baseline. The regression is global, not map-specific.

## Hypothesis ranking, post-evidence

Original ranking (from plan): H1 OIL > H2 tech-pacing > H3 chain combo > H4 geology > H5 AI build > H6 trade.

Updated ranking (from this evidence):

1. **H7 (new) — IR#1 Charcoal-supply regression** dominates everything else (98% of IR#1 blockers). Highest priority.
2. **H2 — Steel tech reach** is the next gate after IR#1 unlocks (115 / 226 IR#2 cases). Tech pacing is real.
3. **H1 — OIL gate** real and material (59 / 226 IR#2 cases) but secondary to Steel-tech.
4. **H4 — geology** modulates the rates (archipelago 2% vs islands 27% IR#1) but the absolute numbers are too low everywhere for placement to be the root cause.
5. **H3 / H5 / H6** are unfalsifiable from this corpus — IR#3+ never fires, so we cannot tell whether the Semiconductor-good blocker is missing-building, missing-recipe-input, or missing-trade. Defer.

## Recommended Phase 2

Branch order, smallest blast radius first:

1. **Phase 2a -- Bisect Charcoal regression.** `git log --oneline -15` and run a single-config sim (e.g. `aoc_simulate --turns 500 --players 4 --seed 1 --map-type continents`) at each commit boundary; check whether any civ produces non-zero Charcoal in `simulation_log.csv` `GoodsStockpiled` or via grep on `economic_report.txt`. Find the breaking commit. This is read-only on `main`, takes ~5 minutes.
2. **Phase 2b -- Once IR#1 firing is restored, re-run the same 40-sim sweep.** Re-evaluate H1/H2/H4 against the recovered baseline. Only after that touch tech-cost or OIL placement.
3. **Phase 2c (deferred)** -- once IR#2 reach > ~50%, re-instrument H3/H5/H6 with building-built and recipe-fired counters per civ.

## Risks

- **Sample-size caveat for IR#3+**: With 0 firings, every IR#3+ blocker reason is technically a cascade artifact, not a primary cause. Drawing any tuning conclusion for IR#3+ from this corpus is invalid.
- **Diag throttle (50 turns)** means a fast-changing blocker (e.g. tech researched but immediately blocked by goods) might emit only the early reason. Most-recent-turn classification mitigates but does not eliminate this.
- **Audit corpus regenerated mid-session** -- the corpus reflects the working tree as of 18:23-18:26 today, not main HEAD. If `git status` shows uncommitted changes to `EconomySimulation.cpp` / `ResourceTypes.cpp`, those are part of the regression surface to bisect.

## Verification gate for Phase 2a

Phase 2a is "fixed" when on a 5-sim continents check with seeds 1-5, p=4, t=1000:
- Mean Charcoal stockpile across surviving civs at turn 500: **> 5 units**.
- Mean civs reaching IR#1 by turn 1000: **>= 70%** (matches pre-regression baseline implied by memory).

After that gate, the original Phase 1 hypothesis ranking can be re-applied.

## Pointers (read-only)

- Recipe 38 (Wood -> Charcoal): `src/simulation/resource/ResourceTypes.cpp` -- find by grepping `RecipeId{38}` or `"Charcoal"`.
- Wood placement: same file, look for `RawNatural` good 78 (Wood) placement rules.
- `economy.totalSupply` aggregation: `src/simulation/resource/EconomySimulation.cpp` -- this is what IR.cpp Path B reads.
- `simulation_log.csv` column `GoodsStockpiled`: total stockpile across all goods per civ -- if this is zero / near-zero for upstream goods even on continents, the recipe is not producing.

**Awaiting user direction: bisect-first (Phase 2a), or different approach.**
