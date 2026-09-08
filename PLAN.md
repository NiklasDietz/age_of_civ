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

**Audited 2026-09-08 against the code.** Most of what this section listed has
since shipped; those entries are recorded below as done rather than deleted, so
the next reader can tell "built" from "never wanted". Only the items under
*Still open* are actually outstanding.

### Shipped since this list was written

- **Loyalty era decay** — era multiplier on foreign pressure plus a +3 loyalty
  floor from Telecom Hub / Research Lab, both in `CityLoyalty.cpp`.
- **Greenhouse / cross-zone crops** — present in `Terrain.hpp` and
  `HexGridLayers.hpp`.
- **Recipe-preference UI** — the city detail screen calls
  `setRecipePreference` (`CityDetailTabs.cpp:1085`).
- **Goods additions** — ELECTRICITY, PHARMACEUTICALS, BATTERIES and LITHIUM all
  exist in `ResourceTypes.hpp`.
- **Tile infrastructure** — `hasRoad`, `hasPowerPole` and `hasPipeline` lanes
  exist. `hasRail` does not; see *Still open*.
- **Instrumentation** — the recipe fire counter is no longer a static
  in-function array of 64. It is `m_recipeFireCount`, an
  `std::array<int32_t, MAX_RECIPES>` with `MAX_RECIPES = 128`, written behind a
  bounds check (`EconomySimulation.cpp:890`).
- **Moon mining (WP-B)** — Lunar Colony is in `SpaceRace.hpp`.
- **Geological resource placement and per-building environment modifiers**
  (both from `IDEAS_ANALYSIS.md`) — `placeGeologyResources` with cratons and
  sedimentary basins, `ResourcePlacementMode::Realistic` as the default, and
  `computeEnvironmentModifier`, called from `EconomySimulation.cpp:781`.
- **Visibility event system** (`todo.txt`) — `VisibilityEventBus` emits,
  `processVisibilityEvents` filters per player through FogOfWar, and the AI
  blackboard consumes the result as `attackTargets` / `bestCitySites`.
- **Monetary progression** (`ideas.txt`) — Barter -> CommodityMoney ->
  GoldStandard -> FiatMoney -> Digital, with copper/silver/gold coin tiers and
  the gold standard gated behind Banking (era 3). The one part of that note not
  built is below.

### Still open

- **`hasRail` tile lane.** Road, power pole and pipeline landed; rail did not.
- **Silver and gold as two separate media.** `ideas.txt` asked for silver as
  the everyday standard of value with gold reserved for international and
  high-value trade, and for the two to fluctuate against each other. The coin
  tiers rank the metals but do not split domestic from international
  settlement.
- **The nine luxury-goods removals** (PEARLS, TOBACCO, IVORY, INCENSE, TEA,
  COFFEE, GEMS, DEUTERIUM, GOLD_CONTACTS). All still present. Deliberate: the
  additions were worth a save-format break, the deletions are not.
- **`IMPROVEMENT_DEFS` / `BuildingDef` tech IDs are display-only** and drift
  from `TechTree.cpp`. Unenforced, so the Encyclopedia can show a wrong tech.
- **Remaining dead recipes.** 21-26 (semiconductor -> aircraft chain) are
  genuinely post-500-turn content. 44 vs 45 and 52 lose profit ranking rather
  than being unreachable. 54 needs river-adjacent Grassland, a rare combo.
- **GA balance sweep.** `BalanceGenome` has **11** slots, not the 13 this file
  claimed: two genes tuned a victory condition that does not exist and were
  removed in `809b1d4`. `religionDominanceFrac` also moved 0.08 -> 0.50, the
  first value that sits inside its own GA bounds of [0.3, 0.8], so every
  previously tuned genome is incomparable.
- **Mechanic synergy audit (WP-A).** See `WORKPACKAGES.md`.
