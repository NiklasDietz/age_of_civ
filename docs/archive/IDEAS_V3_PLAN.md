# Ideas v3 — plan and workpackets

Carry-over of still-open work plus 5 new items from `ideas.txt`.

Status legend: `[ ]` not started · `[~]` in progress · `[x]` done · `[-]` decided to skip

Execution order at the bottom.

---

## Carry-over from prior plans (still open / partial)

### Open from WORKPACKAGES.md
- WP-A3 narrative wonder effects: Pyramids builder charge (done), Taj Mahal GA bonus (done), Oracle free civic (done), Manhattan unlock (done), Colosseum amenity (done). **Still open:** none — all wired or marked deferred.
- WP-C2 cuts/migration: cuts via deprecation done. Save migration done. **Open:** none.
- WP-C3 tile infrastructure: data, BFS bonus, AI build, render, human UI all live. **Open:** pipeline 1-turn settle on routes (current model uses speed doubling which is functionally equivalent — `[-]`).
- WP-C4 Greenhouse plant-a-crop: live including AI heuristic. **Open:** none.
- WP-C6 GA tuning: ran 25 gen × 10 pop × 5 games × 250t pass; current defaults committed. AI mode also ran (output `evolved_summary.txt`, not committed — per-civ).
- WP-D4 1000-turn run: done, full audit report.
- D1/D2 biofuel/biogas tuning: done.
- IMPROVEMENT_DEFS JSON migration: done.

### Open balance gaps from latest audit (`AUDIT_REPORT.md`)
- **Domination victory fires 0/20.** AI doesn't push capital-sweep. Needs `AIController` aggressive-target heuristic, not a balance constant.
- **Religion victory fires 0/20.** Missionary spread reaches equilibrium; no single faith dominates 60% of rivals at 20% city share. Needs spread-strength + apostle behavior tweak.
- **Same-time parity gap.** Culture wins ~600-870, Science wins ~1050-1460, others 1500 timeout. ~600-turn gap. Needs either slower Culture (threshold +) or faster Science (cheaper space race).

### Open from IDEAS_V2_PLAN.md
- Sub-checkboxes inside WPs F-J never updated even though top-level marked done. Cosmetic only.
- Visual cluster fusion (idea 2 from old ideas.txt) — adjacent same-improvement tiles render as one bigger sprite. Skipped; covered in concept by adjacency-arrow overlay.

---

## New WP-K  Trader system overhaul

**Goal**: model trader units as scarce resource, expand via tile/building
slots, scale range with era, allow inter-city slot transfer.

**Status**: `[ ]`

### K1  Civ-wide trade slot pool (global)
- `[x]`  `computeTotalTradeSlots(player, grid)` helper sums:
  - monetary tier baseline (`monetary.maxTradeRoutes()`)
  - `+1` per Market (BuildingId 6) anywhere in civ
  - `+1` per Bank (20)
  - `+2` per Stock Exchange (21)
  - `+1` per Trading Post improvement on any owned tile
  - `+greatPeople.extraTradeSlots` (already wired)
- `[ ]`  No per-city accounting. AI/UI assigns trade routes from any
  city up to the civ-wide cap. Real-world: trade activity isn't bound
  to city size, only to total economic capacity.

### K2  Auto-spawned trader respects civ cap
- `[x]`  `establishTradeRoute` cap = `computeTotalTradeSlots(...)`.
  Active trader count = sum across player's units of UnitClass::Trader.
- `[x]`  Standing-route spawner blocks spawn when at cap.

### K3  Cargo capacity by route type
- `[ ]`  Already partial via `TraderComponent.maxCargoSlots()`. Audit:
  Land: 3, Sea: 6, Air: 4. Adjust if measured trade balance favors one
  too heavily.
- Acceptance: log cargo throughput per route type; ratios match design.

### K4  Range progression — tech-gated, per route type
Range scales as a side effect of transport-tech research, not era.

| Tech researched | Land range | Sea range |
|---|---:|---:|
| (none) | 4 | 8 |
| Animal Husbandry (1) | 6 | — |
| Engineering (6) | 8 | — |
| Apprenticeship (7) | 10 | 10 |
| Metallurgy (8) | — | 12 |
| Industrialization (11) | 16 | 16 |
| Refining (12) | 20 | 22 |
| Mass Production (15) | 24 | 26 |
| Aviation (26) | unlim | unlim (Air route unlocks) |

- `[x]`  Helper `maxTradeRange(player, routeType)` walks the player's
  researched-tech bitmask and returns highest-tier range. Land/Sea
  progress independently. Air = unlimited post-Aviation.
- `[x]`  `establishTradeRoute` rejects when longest unbroken segment
  > range for the route type.
- `[x]`  Logged: `"Trade route rejected: P%u land longest segment 12 > range 6"`.

### K5  Middle-man relay
- `[-]`  Decided to skip — already covered by the existing toll
  mechanism. When A's trader crosses B's territory, B charges a
  per-tile toll (see `TollEntry` in TradeRouteSystem.cpp tile pre-
  scan). That's exactly the middle-man payout. Range still gates
  whether A can reach C at all; toll captures the relay-cut.

### K6  (removed)
Subsumed by K1 — global pool eliminates per-city accounting entirely.

### K7  Trading Posts as range extenders
Civ-6-style: chained relay nodes. Cities and Trading Post improvements
both count as relay points. Trader path is broken into segments by
relay points; each segment must fit within `maxTradeRange(player,
routeType)`. Total reachable distance scales with how many outposts
sit between origin and destination.

- `[x]`  Trading Post `canPlaceImprovement` already permits unowned
  land (no owner gate in the rule).
- `[x]`  `longestRangeGap(path, grid, gameState)` walks the planned
  path, splitting at cities (any owner) and Trading Post tiles.
  `establishTradeRoute` rejects when longest unbroken segment > range.
- `[ ]`  AI builder heuristic for placing Trading Posts on neutral
  land between distant trade partners (open).
- `[x]`  Cities are intrinsic relay points — settling between trade
  partners extends range automatically.

Acceptance: a 4-civ line A-B-C-D where range is 12 and total length
30 routes A↔D as long as A places one Trading Post mid-path and uses
B's territory (with open borders / Transit Treaty) as a second relay.

---

## New WP-L  Analysis script enhancement

**Goal**: introspection on AI decisions + per-tile history + feature
coverage so I can validate AI does sensible things.

**Status**: `[ ]`

### L1  Per-tile snapshot + (deferred) event log
- `[x]`  Tile-snapshot CSV dump at sim end: HeadlessSimulation writes
  `<output>_tiles.csv` next to `simulation_log.csv`. Columns:
  `tile_idx,q,r,terrain,feature,elevation,owner,improvement,resource,
  reserves,has_road,has_pole,has_pipe,greenhouse_crop,natural_wonder`.
- `[~]`  Full per-turn event log: scaffolded `GameState::recordTileEvent`
  + `TileEvent` struct + `tileEvents()` accessor. Hook sites
  (BorderExpansion / Builder / city found-capture / NaturalDisasters
  / Prospect) still pending — invasive cross-cutting change. Existing
  `_events.csv` already covers Tech / Unit / City / Diplomacy events
  per-turn so the gap is mainly per-tile transformation history.

### L2  Per-civ build/research order trace
- `[ ]`  Capture every "Built X" + "Researched X" event with turn,
  player, city. Already mostly logged — add structured CSV export.
- `[ ]`  Compare across maps + seeds: is the AI consistently building
  the right things at the right tech tier?

### L3  AI decision trace (sampled)
- `[ ]`  Per-turn snapshot of: top-scoring building queued, top-scoring
  research, top-scoring spy mission, top-scoring war target, current
  victory-path priority. Sampled every 25 turns to keep size bounded.
- `[ ]`  Dump as CSV next to `simulation_log.csv`.

### L4  Comprehensive mechanic audit script
- `[x]`  `scripts/audit_mechanics.sh` ships. CLI:
  `audit_mechanics.sh [TURNS] [PLAYERS] [SEEDS_LIST] [MAPS_LIST]`.
  Generates `build/audit/REPORT.md` with full mechanic counts +
  victory-type histogram + per-sim outcome + turn distribution.
  Diffable across runs.

---

## New WP-M  3D mode

**Goal**: 3D meshes for tiles, units, cities. Flat hex grid (no planet
curvature). Skybox + lighting + height-based terrain mesh.

**Status**: `[ ]`

### M1  Camera + projection
- `[ ]`  Add 3D perspective camera (orbit + zoom). Toggle 2D ↔ 3D via
  `F2`. 2D path stays as-is for fallback.
- `[ ]`  Vulkan render pipeline: depth buffer, perspective projection
  matrix, view matrix.

### M2  Hex tile mesh
- `[ ]`  Per-tile mesh = hex prism with height = elevation × scalar.
  Coast/Ocean = flat low. Mountain = tall. Hills = mid.
- `[ ]`  Per-vertex color from terrain palette + lighting normal.
- `[ ]`  Single instance buffer with per-tile transform; reuse the same
  hex prism mesh.

### M3  Unit + city models
- `[ ]`  Stub primitive shapes (cone for unit, cube for city). Real
  models can land later.
- `[ ]`  Billboards above each entity for HP / city name (re-use 2D
  text path with depth-disabled overlay pass).

### M4  Improvement + resource visuals
- `[ ]`  Small 3D markers on improvements (Farm = wheat sprite plane,
  Mine = pickaxe shape, etc.). Resource icons projected upright on
  tile center.
- `[ ]`  PowerPole + Pipeline: thin lines connecting tile centers in
  3D space.

### M5  River + border rendering in 3D
- `[ ]`  Rivers as ribbon meshes along hex edges, dipped slightly
  below terrain.
- `[ ]`  Territory borders as extruded outlines on tile boundaries.

### M6  Performance + LOD
- `[ ]`  Frustum cull tiles outside camera. Use index buffer reuse and
  one mesh per tile-type for batching.
- `[ ]`  Optional reduced-detail far-tile mesh (no improvement props).

---

## Audit of new ideas.txt items

All five proposals evaluated:

1. **Limit trader units, autogen still, slot expansion via building/
   tile** — ✓ green light. Maps cleanly to existing `TraderComponent`.
   Slot pool already half-implemented via `monetary.maxTradeRoutes`.
   Scope: medium. Tracked as **WP-K1/K2/K3**.
2. **Range grows with era; middle-man relay** — ✓ green for range
   gate, yellow for relay. Range = small change. Relay = pathing
   rewrite, route splitting, payout accounting. Tracked as **WP-K4**
   (range) and **WP-K5** (relay, stretch).
3. **Per-city vs per-civ slots; transfer between cities** — ✓ green.
   Tracked as **WP-K1** (per-city slots) and **WP-K6** (transfer).
   Recommendation: per-city slots that auto-rebalance, with manual
   override available. Models real-world trade hubs without forcing
   the player to micromanage.
4. **Enhanced analysis covering all features + per-tile history +
   AI decision tracking** — ✓ green. Tracked as **WP-L**. High value
   for catching AI regressions and for the upcoming GA passes.
5. **3D mode — flat hex grid with 3D tiles/units/cities, no planet
   curvature** — ✓ green light **after** WP-K and WP-L land. Render
   pipeline lift but constrained scope (no planet curvature). Tracked
   as **WP-M**.

---

## New WP-N  Tourism / late-era Culture

**Goal**: Culture victory should land late-game like Civ 6 tourism, not
fire at turn 200 from ancient-era theatre stacking.

**Status**: `[ ]`

### N1  Era-gated culture accumulation
- `[ ]`  Per-player era multiplier on culture-toward-victory:
  - Ancient (0): 0.0× (culture exists for civic-unlock only)
  - Classical (1): 0.0×
  - Medieval (2): 0.2×
  - Renaissance (3): 0.5×
  - Industrial (4): 0.8×
  - Modern (5): 1.0×
  - Atomic (6): 1.2×
  - Information (7): 1.5×
- `[ ]`  Apply at `tracker.totalCultureAccumulated += s.culturePerTurn × eraMult`
  in VictoryCondition.cpp.

### N2  Tourism channel (late wonders + great works)
- `[ ]`  Add `tourismPerTurn` accumulator distinct from culture.
- `[ ]`  Sourced from: post-Renaissance wonders, theatre square Great
  Works (already partial via `cultureBonus`).
- `[ ]`  Folded into culture-toward-victory at higher rate (1.5×).
- `[ ]`  Acceptance: Culture wins land turn 800-1200, paired with
  visible tourism-per-turn metric in the 25-turn snapshot.

---

## New WP-O  Trade incentive via storage pressure

**Goal**: Surplus stockpile should create trade pressure. Currently
goods accumulate without cost.

**Status**: `[ ]`

### O1  Auto-sell surplus above cap
- `[ ]`  Per-good soft cap = 80 units (configurable in `BalanceParams`).
- `[ ]`  Each turn, for each city, for each good above cap: sell the
  excess to the global market at 0.7× current market price. Treasury
  gets the gold; stockpile drops to cap.
- `[ ]`  Skip food (`PROCESSED_FOOD`, `WHEAT`) so cities don't starve.
- `[ ]`  Skip strategic uniques like LITHIUM / RARE_EARTH / TITANIUM /
  HELIUM_3 (let those pile for Mars gate).
- `[ ]`  Log: `"P0 Babylon auto-sold 12 IRON_INGOTS surplus (24 gold)"`.

### O2  Export buffer (deferred / optional)
- `[ ]`  Distinct `cityExportBuffer` map, player-designated. Future
  UX for the deeper Civ-6-style designation. Skip until O1 results
  confirm storage pressure is the right lever.

---

## Execution order

1. Close out `AUDIT_REPORT.md` open items first:
   - **Domination + Religion victory pacing.** Quick wins via
     `AIController` aggression tweak + missionary spread bonus. 1-2
     hours each.
2. **WP-K Trader overhaul** (medium, single-pass codebase touch).
3. **WP-L Analysis enhancements** (medium; supports WP-M validation).
4. **WP-M 3D mode** (large; user requested last).

Each WP ends with a build + multi-seed audit run + WORKPACKAGES.md
update.
