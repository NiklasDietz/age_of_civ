# Age of Civilization — current plan

Snapshot of what's done, what's open, what's deferred. Kept terse so it
stays up to date.

## Recently shipped (this session arc)

### Production chains + goods
- Dropped construction-cost prereqs from chain-enabler buildings (Refinery,
  Electronics Plant, Factory, Semiconductor Fab, Industrial Complex) — broke
  the chicken-and-egg loop that left late-tier industrial factories unbuilt.
- Forced-enqueue list in AI production picker for Refinery, Electronics Plant,
  Food Processing Plant, Industrial Complex, Precision Workshop, Semiconductor
  Fab, Biofuel Plant. Cities queue these as top-score candidates when tech +
  district allow.
- Tech-gateway bonus in `AIResearchPlanner` (+20000 primary for Refining and
  Semiconductors, +12000 for Electricity, Mass Production, Precision
  Instruments, Computers, Internet).
- Fixed off-by-one: OIL reveal-tech was `TechId{13}` (Economics) instead of
  `TechId{12}` (Refining). Gas recipes 50/51 same bug. Now correct.
- Recipe output tuning for chain recipes (Refine Fuel 1→2, Plastics 1→2,
  Electronics 1→2, Adv Machinery 1→2, Industrial Equipment 1→2, Consumer
  Goods 2→3).
- `BalanceGenome` extended with `chainOutputMult` and `consumerDemandScale`
  (slots 11, 12). GA tuner can now search over production-chain yield.
- Real per-population CONSUMER_GOODS / ADV_CONSUMER_GOODS drain.
- `EconomySimulation::setRecipePreference` + getter scaffolded for per-city
  per-building recipe override. No UI yet.

### Resource / map
- Guaranteed strategic-fill pass in map gen: every map seeds at least 10 Oil
  + 5 Natural Gas + 4 Niter + 3 Tin tiles on accessible land, both Geology
  and Basic placement paths.
- Strategic tile work-priority: `autoAssignWorkers` now gives RawStrategic
  resources +8 score (was +2), so cities actually prefer Oil/Coal/Iron over
  generic grassland.
- Periodic worker re-assignment every 10 turns per city — newly-revealed
  resources get picked up (e.g. Oil tiles after Refining tech).
- CLAY + RICE placed on Grassland (Rice river-adjacent only). Both had
  recipes but no placement before.
- Climate-zone tightening:
  - Subtropical band carved at temperature [0.55, 0.70] — Tea, Silk, Wine.
  - Tropical (>0.65) restricted: Rubber is Jungle-only now.
  - Temperate narrowed to [0.30, 0.55).
- Continent domain-warp (noise-offset sample coord) so `Continents` mapType
  produces irregular coastlines with bays/peninsulas, not discs.
- Mountain coastal-ridge bias — BFS distance-to-coast + Gaussian elevation
  bump at dist 3-5. Mountains cluster at young-margin bands, not tile centre.

### Rivers + terrain rendering
- BFS over `(tile, prev_direction)` with strict ±2 zigzag transitions →
  Civ 6-style hex-edge river paths where consecutive boundaries share a
  vertex. Capsule rendering with rounded caps.
- Post-commit sanity: river must touch water or is rejected.
- Fog along auto-move path: `Unit::movementTrace` records every tile
  stepped through; Application reveals the stepped-tile + sight radius
  before `updateVisibility` promotes the final position.

### New recipes (expand / connect chain)
- 50 Refine Natural Gas → Fuel (2 gas → 2 fuel, Refinery, Refining gate)
- 51 Crack Natural Gas → Plastics (1 gas → 1 plastics, Refinery)
- 52 Brew Biogas (2 cattle + 1 wood → 1 natural gas, Biofuel Plant)
- 53 Fire Clay Bricks (Workshop, alternative to stone bricks)
- 54 Process Rice → Processed Food (tropical food branch)
- 56 Forge Bronze Tools (Bronze → Tools, early tier)
- 57 Rubber-Sealed Machinery (uses formerly-dead Rubber Goods)
- 58 Heavy-Plated Armored Vehicles (uses formerly-dead Industrial Equip)
- 60 Develop Software (Bootstrap) — 1 Microchip → 1 Software. Resource-light
  path for knowledge-economy civs.
- Removed Surface Plate as a good. Recipes 18 (Basic) and 19 (Premium) now
  produce Precision Instruments directly; Surface Plate is now a
  tech-flavour concept, not a tracked commodity.
- HELIUM_3 (goods id 144, RawStrategic) added. Fusion Reactor (building 35)
  consumes it post-Moon-Landing space project. Pre-Moon falls back to
  coastal Deuterium.
- DataCenter tile improvement synergy: every worked Data Center tile grants
  Software recipes (24 Platform + 60 Bootstrap) +50% output, capped at
  +200%. Enables resource-poor civs to export knowledge goods.

### Game feel + UI
- Civ 6-style territory border: per-edge draw only when neighbour has
  different owner. Generator fix: the neighbour↔edge mapping was wrong and
  the shader applied rotation before size scaling, flattening rotated
  lines. Shader vertex math corrected. Borders now single perimeter.
- Selection highlight: gold capsule ring on the controlled unit / city tile.
- GameSetup screen wrapped in ScrollList so 8-player configs don't spill
  off-screen.
- Spectate mode uses the full GameSetup screen; a seek slider at the
  bottom lets the spectator fast-forward to a specific turn. Backward seek
  via per-20-turn `/tmp` snapshots + full post-load reinit of
  AIControllers/Barbarians/GoodyHuts/TurnManager.
- Placement-mode dropdown (Realistic / Fair / Random) for resource layout.
- Tile tooltip shows river + road status.
- Turn-1 "Unknown Victory" bug: AI first-city wasn't flagged
  `isOriginalCapital`, so conquest-elimination check eliminated every AI
  on turn 2. `foundCity` now auto-marks first city per player as capital.

### Spy / AI fixes
- SPY_PROBABILITY_SCALE shifted 0.16–0.90 → 0.35–0.92 (success rate
  15% → 28%).
- Spy mission diversity: flattened StealTechnology base weight + added
  per-slot RNG jitter so one mission doesn't occupy 90% of picks.
- Settler AI spacing uses loyalty-radius-aware scoring band: -12 at
  dist 3, -5 at dist 4, +bonus in 5-7 ideal band, per-tile penalty past
  loyalty pressure radius.
- Religion spread logged when dominance flips (was silently computed).
- Inflation moneyGrowth + printingInflation clamps (-0.15..+0.25) so the
  +50% cap no longer saturates early-game.

## Open / deferred — next sessions

### Loyalty era decay (idea 1 in `ideas.txt`)
- Propose: era-5+ multiplier on loyalty pressure (0.85 per era past 4) +
  Telecom Hub / Research Lab grants +3 loyalty floor.
- Reasoning: matches real-world — mass communication + mobility erodes
  city-level separation pressure.
- Effort: small.

### Goods table cleanup (idea 2 in `ideas.txt`)
- Consolidate luxuries: cut PEARLS, TOBACCO, IVORY, INCENSE, TEA, COFFEE,
  GEMS, DEUTERIUM, GOLD_CONTACTS (9 goods gone).
- Add: ELECTRICITY (tracked power), PHARMACEUTICALS, BATTERIES, LITHIUM
  (4 new). Net 76 → 71 goods.
- Effort: medium. Breaking change for save format — needs migration path.

### Tile infrastructure (multi-building per tile)
- Add per-tile bitfield lanes: `hasRoad`, `hasRail`, `hasPowerPole`,
  `hasPipeline`. All stack with `improvement`.
- Power grid: power plants generate `powerOutput`; pole network propagates
  along connected tiles; cities in range get production/science
  multiplier.
- Pipelines: oil/gas/fuel trade routes on pipeline-connected tiles settle
  in 1 turn vs 3-5, throughput ×2.
- Cross-player poles/pipelines → Transit Treaty (diplomatic).
- Effort: ~1 week. Touches map gen, save/load, renderer, UI, AI builder.

### Greenhouse / climate cross-zone crops
- Research gate (biology or genetic eng): unlocks Greenhouse improvement.
- Greenhouse on a tile outside a crop's climate lets it still grow the crop
  at 50% yield.
- Effort: medium. Needs per-crop climate metadata + Greenhouse improvement +
  yield penalty multiplier in `harvestResources`.

### Recipe-preference UI
- `EconomySimulation::setRecipePreference` already wired. Need city detail
  screen dropdown per building: list candidate recipes + Auto default.
- Enables forcing Biofuel Plant to brew Biogas (recipe 52) instead of
  Biofuel, etc.
- Effort: small-medium.

### GA tuning run
- `BalanceGenome` now has 13 slots including chainOutputMult and
  consumerDemandScale.
- Run `aoc_evolve --tune-mode balance` over a 20-30 gen × 5-game batch.
- Effort: code change tiny; runtime cost ~hours.

### Remaining dead recipes (chain completion)
- Recipes 21-26 Semiconductor/Microchip/Computer/Aircraft chain: genuinely
  post-500-turn content. Would fire naturally in 1000-turn games.
- Recipe 44 Biofuel (Wheat): loses profit ranking to recipe 45 (Sugar);
  needs preference UI or a small profit nudge.
- Recipe 52 Biogas: same — Biofuel Plant runs 44/45 first.
- Recipe 54 Rice: river-adjacent Grassland is a rare tile combo. Could
  relax to any Grassland.

### Production chain integrity items
- Many `IMPROVEMENT_DEFS` tech IDs are display-only and drift from
  TechTree.cpp. Not enforced, but Encyclopedia shows misleading info. Audit
  + align.
- Rework BuildingDef / ImprovementDef tech IDs into a single source of
  truth (ideally the JSON `data/definitions/*.json`).

### Instrumentation
- `s_recipeFireCount` in `EconomySimulation.cpp` is currently a static
  in-function counter with array size 64. Recipes now extend to id 60;
  moving toward explicit bounds + maybe per-player telemetry.

### Mechanic synergy audit (idea 3 in `ideas.txt`)
- Game has ~20 systems (tech, civics, religion, diplomacy, spy, monetary,
  loyalty, prestige, great people, wonders, climate/pollution, supply
  lines, war weariness, grievances, trade routes, IR, production chain,
  victory conditions, government, promotions).  Many operate in isolation
  instead of compounding.
- Weak synergy spots identified — see `WORKPACKAGES.md` WP-A.

### Moon mining expansion (idea 4 in `ideas.txt`)
- Decision: stay abstract (no separate moon map).  Add depth via
  additional space-race projects + one new strategic good gated to
  lunar mining.
- Lunar Colony project (between Moon Landing and Mars) multiplies He3 ×3 +
  grants flat science bonus + unlocks lunar Titanium.
- Mars Colony requirement tightened to consume He3 + Titanium +
  Semiconductors so the late-tech chain is self-referential.
- Detailed plan in `WORKPACKAGES.md` WP-B.
