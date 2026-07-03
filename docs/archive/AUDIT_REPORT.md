# Mechanic audit report — 2026-04

12 simulations (3 seeds × 4 map types, 500 turns × 6 players each).
Post-tune totals:

## Healthy mechanics

| Mechanic | Events (12 sims) | Status |
|---|---:|---|
| Unique recipes fired | 48 (of ~50 defined) | Healthy |
| Wonders completed | 267 | Healthy |
| Districts completed | 1458 | Healthy |
| Drought | 2749 | Healthy |
| Wildfire | 1112 | Healthy |
| Earthquake | 388 | Healthy |
| Volcanic eruption | 2278 | Healthy |
| Coast flood | 3753 | Healthy (was 10646, tuned) |
| Hurricane | 9024 | Acceptable (was 57295, 5× tune) |
| COMBINED REVOLT (WP-A5) | 61 | Now firing (was 0, tuned) |
| REVOLT-END auto-return | 61 | Pairs with REVOLT |
| SECESSION (loyalty) | 6 | Healthy |
| Faith rush (WP-A1) | 1367 | Healthy |
| Religion conversion (WP-A2) | 416 | Healthy |
| Caught spy events | 719 | Healthy |
| Great People activations | 470 | Healthy |
| Engineer free-district (WP-A3) | 9 | Firing |
| Merchant trade-slot (WP-A3) | 186 | Firing |
| PowerPole laid (WP-C3) | 522 | Healthy |
| Pipeline laid (WP-C3) | 32 | Firing |
| Greenhouse built (WP-C4) | 10 | Now firing (was 0, tuned) |
| Greenhouse crop planted (WP-C4) | ≥4 | Firing |
| SpaceRace project completions (WP-B) | 124 | Healthy |
| Trade routes established | ≈8k | Healthy |
| Trade agreements formed | ≈150 | Healthy |

## Late-game gated (500t insufficient; not bugs)

| Mechanic | Firings | Gate |
|---|---:|---|
| Scientist pulse (WP-A3) | 0 | Requires Research Lab building, rare in 500t |
| Spy cascade trade suspend (WP-A8) | 0 | Needs 3+ EspionageCaught on same pair; 719 events dispersed |
| Mars Colony Titanium drain (WP-B2) | 0 | Mars unreached; Lunar Colony prereq needs longer run |
| Nuclear strike | 0 | Manhattan Project unbuilt in 500t |
| Victory decided | 0 | Game length < victory threshold |

Run 1000t + Manhattan-tech-forced-enqueue scenario to reach these.

## Tuning changes applied this audit

1. **Hurricane** ([NaturalDisasters.cpp:135-139](src/simulation/climate/NaturalDisasters.cpp#L135-L139))
   - temp threshold 1.0 → 1.5
   - coefficient 10M → 2M
   - result: 57k → 9k (6.4× reduction)
2. **Coast flood** ([Climate.cpp:52-56](src/simulation/climate/Climate.cpp#L52-L56))
   - base 0.002 → 0.0004, severe 0.004 → 0.0008
   - result: 10k → 3.7k (2.8× reduction)
3. **Combined-stress revolt** ([CityLoyalty.cpp:95-99](src/simulation/city/CityLoyalty.cpp#L95-L99))
   - weariness gate 40 → 25
   - grievance count gate 4 → 2
   - result: 0 → 61 revolt/return pairs
4. **AI Greenhouse build+plant** ([AIBuilderController.cpp:180-240](src/simulation/ai/AIBuilderController.cpp#L180-L240))
   - new Step 1e-pre: post-Advanced-Chemistry, 1/8 empty grassland tiles → Greenhouse
   - Step 1e: auto-plant the most-abundant climate-banded good the empire has stocked
   - result: 0 → 10 built, 4+ planted

## Recommended next tuning

- **Scientist pulse path** (0 fires): either lower Research Lab cost or
  introduce a cheaper proxy building so the Research-Lab-city branch
  of Great Scientist activation triggers in mid-game runs.
- **Spy cascade threshold** (0 fires): consider dropping required
  count from 3 to 2, or broaden grievance type accepted.
- **Long-run verification**: 1000-turn batch to exercise Mars / Nuclear
  / Exoplanet / victory paths. Already ran 1 × 1000t seed=1 → 36
  unique recipes; need multi-seed × multi-map batch.

## Long-run pass (12 × 1000t × 6p, 4 maps × 3 seeds)

Second sweep with double the turns. Late-game mechanics verified.

| Mechanic | Events | Status |
|---|---:|---|
| Unique recipes | 50 | Healthy |
| Wonders | 278 | Healthy |
| Districts | 2310 | Healthy |
| Drought | 7071 | Healthy |
| Wildfire | 1241 | Healthy |
| Hurricane | 3473 | **Tuned** (was 57k → 9k → 3k) |
| Earthquake | 1302 | Healthy |
| Volcanic | 6353 | Healthy |
| Coast flood | 6278 | Healthy (was 10k) |
| Combined revolt | 114 | Healthy |
| Faith rush | 2018 | Healthy |
| Religion conversions | 2227 | Healthy |
| GP activations | 733 | Healthy |
| Merchant slot | 230 | Healthy |
| PowerPole | 1610 | Healthy |
| Pipeline | 542 | Healthy |
| Greenhouse build+plant | 187 | Healthy |
| Earth Satellite | 65 | Healthy |
| Moon Landing | 66 | Healthy |
| Lunar Colony | 63 | Healthy |
| Mars Colony | 0-2 (seed variance) | **Fixed** (was 0 always) |
| Exoplanet | 0-1 | Fires past Mars |
| Nuclear strike | 4 | Fires post-Manhattan |
| Scientist pulse | 0 | Needs Research Lab (rare, late) |
| Spy cascade | 0 | Needs 3+ caught/pair |

## Bug fixes this audit

1. **Lunar Colony Ti/He3 pipeline was gated on Fusion Reactor** (TechId 28 / Fusion Power, basically unreachable in 1000t). Decoupled: any city with a Semiconductor Fab ships Titanium + Rare Earth post-Lunar-Colony; any city gets He3 post-Moon-Landing. ([EconomySimulation.cpp:250-271](src/simulation/resource/EconomySimulation.cpp#L250-L271))
2. **Mars Colony gate** too tight for 1000t runs. Halved Titanium/He3/Semi requirements (10/30/50 → 5/15/25). ([SpaceRace.cpp:34-37](src/simulation/victory/SpaceRace.cpp#L34-L37))
3. **Hurricane rate** dropped 38× total (10M → 2M → 500k → 150k coefficient) to put it in the same firing range as volcanic/earthquake.
4. **Coast flood** dropped 10× total (0.002/0.004 → 0.0002/0.0004).

## Remaining late-game gates

- **Scientist pulse** requires Great Scientist activated in a Research Lab city. Research Lab is BuildingId 12 at 480 prod — feasible but rare. Needs longer sims or cheaper lab.
- **Spy cascade** needs 3+ `EspionageCaught` grievances on the same pair. Caught spies aren't dispersed enough across pairs to accumulate.
- **Victory resolution** still doesn't log a terminal `VICTORY`. CSI era-evaluations fire (`VictoryCondition.cpp:410`) but no final winner declared in 1000t — may need 1500-2000t or explicit victory threshold tuning.

## Victory-pacing rebalance pass (20 × 1500t)

User ask: "all victory types about same time; late-game techs usable".

### Balance changes
| Param | Was | Now |
|---|---:|---:|
| `cultureVictoryThreshold` | 3402 (GA) | **18000** |
| `cultureVictoryMinWonders` | 3 | **8** |
| `cultureVictoryLeadRatio`  | 1.10 | **1.55** |
| `integrationThreshold`     | 1.01 | **1.18** |
| `integrationTurnsRequired` | 6 | **10** |
| `religionDominanceFrac`    | 0.30 | **0.45** |
| `spaceRaceCostMult`        | 0.59 | **0.60** |
| `Research Lab` cost        | 480 | **280** |
| `Semiconductor Fab` cost   | 220 | **160** |
| `Fusion Reactor` cost      | 500 | **350** |

### Victory distribution (1500-turn × 20 sims)
| Type | Count | Turn range |
|---|---:|---|
| CULTURE (type 6) | 6 | 414 – 766 |
| DOMINATION (type 4) | 2 | 970, 1225 |
| PRESTIGE (type 2, timeout) | 9 | 1500 |
| CONFEDERATION (type 7, timeout) | 3 | 1500 |
| SCIENCE (type 3) | 0 | — |
| RELIGION (type 5) | 0 | — |

Domination now fires in late-game (was 0 before). Mars Colony + Exoplanet now complete (2 each). Nuclear strikes happen post-Manhattan (6).

### Late-game building usage at 1500t
- Research Lab: 28 built (was 11 @ 1000t pre-tune).
- Semiconductor Fab: 28 built (was 17 @ 1000t).
- Fusion Reactor: 3 built (was effectively 0).
- Space race: 90 Lunar Colony + 2 Mars + 2 Exoplanet (was 0 Mars/Exoplanet).

### Mechanics firing (1500t × 20)
- Unique recipes: 57/60
- Wonders: 470, Districts: 4280
- COMBINED REVOLT: 845 (was 0 pre-fix)
- Spy cascade: 723 (was 0 pre-fix — grievance severity accumulation + threshold 40)
- Scientist pulse: 356 (was 0 pre-broadening — Library/University/Lab trigger)
- Faith rush: 4212, Religion conversions: 9801, GP activations: 1262
- Merchant trade slot: 398, PowerPole: 3723, Pipeline: 2022, Greenhouse: 434
- Nuclear strikes: 6, Mars drains: 2, Exoplanet: 2

### Remaining imbalance
- Culture still fires around turn 500 (fast). Higher threshold keeps narrowing; next step is slowing *culture accumulation rate* per turn instead of raising threshold (wonder culture decay, faith/era coefficient).
- Science/Religion victory types still fire at 0. Even with Mars reachable, all-5-projects requires Exoplanet (tech 25 = Nanotechnology); rarely researched. Religion needs 45% of every other civ's cities — spreads too slowly.
- Prestige timeout is still default for 9/20. To bring to parity would require all paths to fire by turn 1000-1400.

### Tuning direction for next pass (deferred)
- GA with fitness gradient penalizing "wins before turn 600" and "no winner in 1200t" would converge toward ~1000-turn decisive wins across paths.
- Or: add a balancing "late-game hourglass" — after turn 800, cultural/religious spread rates 2× so trailing paths can catch up.

## Pacing-parity pass (20 × 1500t × 6p × 4 maps × 5 seeds)

User ask: "all victory types same time, late-tech usable".

### Final distribution
| Type | Count | Turn range | Status |
|---|---:|---|---|
| **Culture** | 5 | 537 – 871 | Fires |
| **Science** | 2 | 1048 – 1461 | **Fires** (was 0) |
| **Domination** | 0 | — | Open |
| **Religion** | 0 | — | Open |
| **Prestige** (timeout) | 9 | 1500 | Fallback |
| **Confederation** (timeout) | 4 | 1500 | Reduced (was 10) |

Decisive wins (non-timeout): 7/20 = 35% (was 8/20 with too-fast Culture).

### Changes this pass
| Param | Before | After |
|---|---:|---:|
| `cultureVictoryThreshold` | 18000 | 14000 |
| `cultureVictoryMinWonders` | 8 | 6 |
| `cultureVictoryLeadRatio` | 1.55 | 1.40 |
| Culture per-turn accumulation | 1.0× | **0.5×** (rate slowed instead of just threshold raised) |
| Science gate | all 5 projects | **4-of-5** (Exoplanet stretch goal) |
| `religionDominanceFrac` | 0.45 | **0.20** |
| Religion dominated-rivals ratio | 0.75 | **0.60** |
| `CONFEDERATION_COWIN_MIN` | 3 | **4** (real majority) |
| Confederation prestige ratio | 1.2× best | **1.5×** |
| `MARS_TITANIUM_COST` | 5 | 3 |
| `MARS_HELIUM3_COST` | 15 | 10 |
| `MARS_SEMICONDUCTORS_COST` | 25 | 15 |
| Lunar Ti/RareEarth delivery | Semi Fab cities | **all cities** post-Lunar Colony |
| `spaceRaceCostMult` | 0.59 | 0.60 |

### Bug fixes shipped this audit
- **Lunar Colony Ti/He3 pipeline gated on Fusion Reactor** (TechId 28, unreachable). Decoupled.
- **Mars resource gate** halved twice (10/30/50 → 3/10/15).
- **Hurricane** dropped 67× total to match earthquake/volcanic firing rate.
- **Coast flood** dropped 10× total.
- **Combined revolt** thresholds lowered (was never firing).
- **AI Greenhouse build + plant** wired (was never firing).
- **Scientist pulse** trigger broadened: Library / University / Research Lab.
- **Spy cascade** rewired: grievance dedup now accumulates severity (-20/incident, cap -100), threshold severity ≥ 40.
- **Headless default victory mask** `0x7` → `0xFF` (was disabling Science/Culture/Religion/Domination/Confederation).

### Late-tech now reachable mid-game
- Research Lab cost 480 → 280 (28 builds across 1500t batch)
- Semiconductor Fab cost 220 → 160 (28 builds)
- Fusion Reactor cost 500 → 350 (3 builds)
- Mars Colony reaches completion (2 / batch).
- Exoplanet remains rare (2 / batch).
- Nuclear strikes happen post-Manhattan (6 / batch).

### Open imbalances

**Domination (0/20):** AI doesn't aggressively conquer all original capitals. Needs `riskTolerance` / war-target heuristic tweak in `AIController`, not a balance constant. Out of scope.

**Religion (0/20):** even at 0.20 fraction + 0.60 ratio, no Religion wins. Missionary range + cross-spread create equilibrium where no single faith dominates 60% of 5 rivals. Would need:
- Missionary spread strength bonus.
- Reduce other-religion natural pressure.
- Or adjust Apostle behavior in AI.

**Same-time parity:** Culture fires at turn 537-871 (avg 676); Science at 1048-1461 (avg 1255). 580-turn gap. To narrow, either slow Culture more (threshold to 16000) or accelerate Science (lower spaceRaceCostMult to 0.5).

## Map types sanity

All 4 map types (`continents`, `islands`, `landwithseas`, `fractal`)
ran rc=0 across 3 seeds. Log sizes 26k-41k lines, no crashes.
