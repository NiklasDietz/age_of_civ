# Age of Civilization — work packages

Tracking file for ongoing + planned work.  Status legend:
`[ ]` not started · `[~]` in progress · `[x]` done · `[-]` decided to skip

Each WP lists scope, acceptance signal, and optional sub-tasks.

---

## WP-A  Mechanic synergy audit

**Goal**: wire isolated systems into each other so gameplay decisions
compound instead of running in parallel silos.

**Status**: `[ ]`

### A1  Religion × economy
- `[ ]`  Cities with a dominant religion get a small production or gold
  bonus based on the founder's chosen follower belief.
- `[ ]`  Faith can be spent to rush a building (once per turn per city,
  cost scales with building tier).
- Acceptance: in a sim batch, religious civs show a measurably different
  production profile from secular civs.

### A2  Religion × diplomacy
- `[ ]`  Shared dominant religion grants a +10 relation modifier per
  city sharing it (capped).
- `[ ]`  Opposing religions grant -8 if the other civ converts one of
  your cities.
- Acceptance: alliance + war rates correlate with religion overlap in a
  sim batch.

### A3  Great People → permanent effects
- `[ ]`  Great Engineer: place on a tile, consumes the GP and creates a
  district (Industrial by default) at no production cost.
- `[ ]`  Great Scientist: +X science for 20 turns if used in a Research
  Lab city; otherwise grants one free tech-progress jolt.
- `[ ]`  Great Merchant: grants a permanent trade-route slot.
- Acceptance: audit log shows Great Person "uses" actually create
  something trackable, not just vanish.

### A4  Climate ↔ pollution feedback
- `[ ]`  Verify pollution actually feeds into climate-change trend
  (currently unclear whether Pollution.cpp raises climateDelta).
- `[ ]`  Global climate level raises disaster rate: each +0.1 climate
  delta → +5% disaster probability per turn.
- `[ ]`  Climate-induced disaster damage adds war-weariness-like unhappiness.
- Acceptance: sim shows higher-pollution civs suffer more
  disasters + amenity loss.

### A5  Supply / war weariness / grievance stacking
- `[ ]`  When all three high simultaneously: city risks "revolt" — a
  soft secession that flips the city to Free-City for 10 turns.
- Acceptance: sim log shows distinct revolt events; currently only
  loyalty-driven secession exists.

### A6  Tech diffusion via proximity / trade
- `[ ]`  A civ with a bilateral trade deal to a tech-leader gets +10%
  research speed on techs the partner has already completed.
- Acceptance: weaker civs catch up tech-wise when allied with leaders.

### A7  Wonder-per-era impact
- `[ ]`  Audit wonders: each era should have at least one wonder that
  enables a unique strategy (not just flat bonus).
- `[ ]`  Add era-multiplier on wonder bonuses so Classical wonders stay
  relevant into Modern era (half-life decay).
- Acceptance: wonder selection matters across eras rather than one
  dominant "always-build" wonder.

### A8  Spying → cascading grievance
- `[ ]`  Currently a caught spy emits grievance + relation penalty.
  Extend: repeat spy-fail chains trigger trade-agreement suspension.
- Acceptance: sim shows spying-heavy civs lose trade partners over time.

---

## WP-B  Moon resource expansion (abstract, no separate map)

**Goal**: give Moon Landing more than just "flip Fusion Reactor to He3."
Open late-game chain without needing a second map surface.

**Status**: `[ ]`

### B1  Add Lunar Colony space project
- `[ ]`  New `SpaceProjectId::LunarColony` between `MoonLanding` and
  `MarsColony`. Requires He3 stockpile + plastics + steel.
- `[ ]`  On completion: post-landing He3 delivery multiplied ×3; +20
  flat science/turn empire-wide ("low-gravity physics").
- Acceptance: completing project shows in Space Race UI + He3 rate
  triples in sim log.

### B2  Titanium good + lunar extraction
- `[ ]`  Add `TITANIUM` (RawStrategic, id 145).  Placement: none (lunar
  only).
- `[ ]`  Completing Lunar Colony grants 1 Titanium/turn to each city
  with a Semiconductor Fab (lunar ore refined with fab).
- `[ ]`  Mars Colony project (existing) now requires Titanium + He3 +
  Semiconductors.
- Acceptance: Mars Colony gated on Titanium supply; civs without
  Lunar Colony can't ship Mars.

### B3  Rare Earth Elements (stretch)
- `[ ]`  Add `RARE_EARTH` (RawStrategic, id 146).  Placement: rare on
  mountain tiles (0.02 chance); OR bonus from Lunar Colony.
- `[ ]`  Consumed by Semiconductor recipe as alternate input.
- Acceptance: another path to Semiconductors exists.

### B4  Decision doc
- `[x]`  "Stay abstract vs separate moon map" → abstract wins.  Noted
  in `PLAN.md` under "Moon mining expansion."

---

## WP-C  Previously-deferred items

### C1  Loyalty era decay
- `[ ]`  Era-5+ multiplier on loyalty pressure (0.85 per era past 4).
- `[ ]`  Telecom Hub + Research Lab grant +3 loyalty floor.
- `[ ]`  Expose as BalanceGenome scalar (`loyaltyEraDecay`).
- Acceptance: late-game secession rates drop; early-game still punchy.

### C2  Goods table cleanup
- `[ ]`  Audit 76 current goods → propose 71 after cull.
- `[ ]`  Cuts: PEARLS, TOBACCO, IVORY, INCENSE, TEA, COFFEE, GEMS,
  DEUTERIUM, GOLD_CONTACTS (need save-migration map).
- `[ ]`  Adds: ELECTRICITY (tracked power), PHARMACEUTICALS, BATTERIES,
  LITHIUM.
- `[ ]`  Save/load migration stub so older saves still load.
- Acceptance: no dead-end goods + no goods with no producer.

### C3  Tile infrastructure (multi-building per tile)
- `[ ]`  Per-tile bitfield lanes: `hasRoad`, `hasRail`, `hasPowerPole`,
  `hasPipeline` — all stack with existing `improvement`.
- `[ ]`  Power grid propagation via poles.  Cities in range of
  connected pole get +X production multiplier.
- `[ ]`  Pipelines: +100% throughput + 1-turn settle on oil/gas/fuel
  trade routes.
- `[ ]`  Cross-player transit → Transit Treaty (diplomatic agreement).
- `[ ]`  UI: pole/pipeline build orders via Builder; render layer.
- Acceptance: map shows pole networks; disconnected cities suffer
  production penalty.

### C4  Greenhouse cross-zone crops
- `[ ]`  Tech "Genetic Engineering" (or Biology) unlocks Greenhouse
  improvement.
- `[ ]`  Per-crop climate metadata in `GoodDef`.
- `[ ]`  Greenhouse on off-climate tile lets the crop grow at 50%
  yield.
- Acceptance: subtropical civ can grow bananas in temperate with
  Greenhouse.

### C5  Recipe-preference UI
- `[x]`  `EconomySimulation::setRecipePreference` + getter (done).
- `[ ]`  City-detail screen dropdown per industrial building: list
  matching recipes + Auto default.
- `[ ]`  Set preference via click; clear by selecting Auto.
- Acceptance: can force Biofuel Plant to brew Biogas (recipe 52).

### C6  GA tuning run
- `[ ]`  Launch `aoc_evolve --tune-mode balance` 20-30 gen × 5 game ×
  5 seeds.
- `[ ]`  Review evolved BalanceParams, commit winning genome as new
  defaults.
- Acceptance: fitness metrics (entropy, length, gini, decisive share)
  improve over baseline.

### C7  IMPROVEMENT_DEFS / BuildingDef tech-id audit
- `[ ]`  Align `IMPROVEMENT_DEFS` tech IDs with `TechTree.cpp`.
  Several defs point at wrong tech (e.g. DataCenter was Surface Plate
  18 instead of Computers 16; fixed).
- `[ ]`  Migrate source of truth to `data/definitions/*.json`.
- Acceptance: Encyclopedia shows correct tech gate for every item.

### C8  Recipe instrumentation hardening
- `[ ]`  Replace ad-hoc `s_recipeFireCount` static with a per-game
  counter on `EconomySimulation` + serializable telemetry.
- Acceptance: can dump "recipes fired / turn / player" to CSV for
  external audit tool.

---

## WP-D  Remaining dead-recipe items

### D1  Recipe 44 (Biofuel from Wheat) never wins vs 45 (Sugar)
- `[ ]`  Nudge 44 profit + output so a Wheat-heavy civ picks it.
- Or: let WP-C5 recipe-preference UI solve it.

### D2  Recipe 52 (Biogas) blocked by Biofuel Plant running 44/45
- `[ ]`  Solved by WP-C5 preference UI, or
- `[ ]`  Bump biogas profitability so ranker picks it first.

### D3  Recipe 54 (Rice) needs river-adjacent Grassland
- `[ ]`  Relax to any Grassland in temperate band (remove river
  constraint).  Or keep river + add lakes to qualify.

### D4  Late-era chain 21–26 doesn't fire in 500-turn games
- `[x]`  Analysed: legitimately post-500t.  Not fixing in 500t games.
- `[ ]`  Verify firing in 1000-turn game-length option.

---

## WP-E  Cross-session hygiene

### E1  Session-bound PLAN.md maintenance
- `[ ]`  Keep PLAN.md roster in sync with actual ships.  Already
  initialized.  Update on each session end.

### E2  Cold-session smoke-test script
- `[ ]`  One-liner that builds + runs 5-seed batch + dumps recipe-fire
  count.  Used as a regression check after each change.
- Draft location: `scripts/smoke_recipes.sh`.
