# Ideas v2 — plan and workpackets

Follows from the 4 ideas logged in `ideas.txt` (plus the hover-arrows UX
request). Each workpacket has scope, acceptance, and a status marker.

Status legend: `[ ]` not started · `[~]` in progress · `[x]` done · `[-]` decided to skip

---

## WP-F  Profile-Guided Optimization (PGO)

**Goal**: ship build plumbing that measurably speeds up hot-loop code
(TurnProcessor, CityScience, ProductionSystem ranker). Two-stage GCC
workflow: instrumented build → training run → optimized rebuild.

**Status**: `[x]`

### F1  CMake PGO plumbing
- `[ ]`  Add `AOC_PGO_PHASE` cache variable: `off` (default) / `generate` / `use`.
  - `generate`: `-fprofile-generate=<repo>/build/pgo_profiles -fprofile-dir=<same>`
  - `use`: `-fprofile-use=<same> -fprofile-correction`
- `[ ]`  Guard so phase flags only apply when compiler is GCC.
- `[ ]`  Document in `scripts/pgo_build.sh` the 3-step pipeline:
  1. Reconfigure `-DAOC_PGO_PHASE=generate`, rebuild, run training.
  2. Training run = `aoc_simulate --turns 200 --players 6 --seed 1`.
  3. Reconfigure `-DAOC_PGO_PHASE=use`, rebuild.
- Acceptance: two clean builds via the script; timing comparison in a
  short report produced by the script (runtime of a fixed sim, before
  vs after). Sandbox-blocked runtime: user invokes.

---

## WP-G  Adjacency scaling for improvements

**Goal**: numeric adjacency bonuses for farm / biogas / solar / wind /
similar. Makes clusters lucrative and gives biogas a "3-adjacent = one
OIL tile" identity.

**Status**: `[x]`

### G1  Generalized improvement-adjacency helper
- `[ ]`  Extract existing farm-adjacency pattern into a generic helper
  `computeImprovementClusterBonus(grid, idx, improvement) -> TileYield`.
  Works for same-improvement adjacency and uniform-bonus rules.

### G2  Biogas cluster → OIL-equivalent
- `[ ]`  Rule: each adjacent BiogasPlant tile gives the center tile
  +1 prod +0.5 food-offset. At 3+ adjacent biogas, +3 prod +1 food
  (net flips the -1 food penalty). Equivalent to a single OIL tile at
  full cluster; single biogas remains clearly worse.

### G3  Solar / Wind adjacency
- `[ ]`  SolarFarm: +1 gold +1 sci per adjacent SolarFarm, cap +2.
- `[ ]`  WindFarm: +1 prod per adjacent WindFarm, cap +2.

### G4  Hover adjacency-arrow overlay (see WP-J)
- Linked: WP-J handles the UX layer.

- Acceptance: cluster yields visible in per-city totals; biogas farm
  of 4 tiles near a city outyields a lone OIL tile on productive prod
  but not on gold (biogas = renewable but cheaper to stockpile than
  oil trade).

---

## WP-H  AI takeover / observer mode

**Goal**: in an auto-AI sim, user can click "Take Over" on any AI
player. Control transfers, fog renders from that player's POV, AI for
that slot disabled.

**Status**: `[x]`

### H1  Takeover trigger
- `[ ]`  Button in Diplomacy screen ("Take Over") per listed player.
- `[ ]`  On click: set `player.setHuman(true)`, disable matching
  `AIController` (skip its step in TurnProcessor), set the active
  camera's viewing player id.

### H2  Fog and vision
- `[ ]`  Verify fog-of-war layer uses the *current viewing player id*
  (not `PlayerId{0}` hard-coded). Flip to a member `m_viewingPlayer`
  if needed; default to 0 but updated by takeover.
- `[ ]`  Camera re-centers on taken-over player's capital on takeover.

- Acceptance: auto-sim running → click Take Over → next turn is manual
  end-turn, fog shows only that civ's known tiles, enemy units drawn
  only when in sight range.

---

## WP-I  Wind + rain-shadow (planet-sim slice)

**Goal**: incremental upgrade of MapGenerator realism without rewriting
to a full planet sim. Delivers latitude-aware winds + mountain rain
shadow. Skip ocean currents and full pressure-cell simulation.

**Status**: `[x]`

### I1  Wind band model
- `[ ]`  Add `windDirection(row, height)` function returning one of
  the 6 hex directions. Three bands per hemisphere:
  - equatorial (trade winds, easterly)
  - mid-lat (westerlies)
  - polar (easterly)
- `[ ]`  Stored per tile as `uint8_t` wind direction bits in HexGrid
  (new `m_windDir` vector, 3 bits + flags).

### I2  Rain shadow
- `[ ]`  For each non-water land tile, walk 2 tiles upwind. If any is
  Mountain, mark tile as rain-shadow (dryer). Effect: tile has 15%
  higher chance of Desert / Tundra during climate assignment.
- `[ ]`  Inverse: tile upwind of coast gets +10% forest/jungle chance
  (moist onshore flow).

### I3  Regen verification
- `[ ]`  Spot-check generated map looks plausible (mountains on
  windward coasts = rainforest, leeward = desert).

- Acceptance: visible biome contrast across mountain ranges;
  simulation still deterministic per seed (only map-gen changes).

---

## WP-J  Hover adjacency-arrow overlay

**Goal**: player hovers a tile → arrows from that tile toward each
adjacent tile that triggers an adjacency bonus (farms, biogas, solar,
wind, district). Shows the *why* behind the yield numbers.

**Status**: `[x]`

### J1  Data source
- `[ ]`  New API `collectAdjacencyArrows(grid, hoveredIdx)` returning
  `std::vector<AdjacencyArrow>` with `{fromTile, toTile, kind, magnitude}`.
  Checks farm×farm, biogas×biogas, solar×solar, wind×wind, and
  district-to-district adjacency.

### J2  Render
- `[ ]`  New overlay draw path in `MapOverlays.cpp`: on hover, draw a
  colored arrow (line + arrowhead triangle) from hovered tile center
  to each qualifying neighbor. Color by kind:
  - Farm: wheat-yellow
  - Biogas: green
  - Solar: gold
  - Wind: cyan
  - District: orange

### J3  Hook into input
- `[ ]`  Application tracks `m_hoveredTile`. Pass into overlay render
  each frame. Clear on off-map hover.

- Acceptance: moving mouse over a farm tile shows small yellow arrows
  to every adjacent farm. Single-tile improvements with no qualifying
  neighbor show nothing.

---

## Execution order

1. WP-F (PGO) — small, ships build infra today.
2. WP-G (adjacency scaling) — data + logic, contained.
3. WP-J (hover arrows) — builds on G data.
4. WP-H (AI takeover) — UI plumbing, medium.
5. WP-I (wind + rain shadow) — map-gen extension, deferred if earlier
   work pressures timeline.

Each batch ends with a full build + WORKPACKAGES.md update.
