# Physics-first plate-tectonic rewrite

Started 2026-05-06. Replaces the OLD Voronoi-era plate-tectonic + continent-creation pipeline with a real physics simulation on a global lat/lon raster (`SphereField`) at 0.5 deg resolution.

This document is the canonical reference. Obsolete intermediate snapshots are archived under `docs/archive/`: `SPHERE_MIGRATION.md`, `PHYSICS_REWRITE_CONTINUE.md` (P7 archive 2026-05-06).

## Architecture

- **Authoritative state**: `aoc::map::gen::SphereField` (`include/aoc/map/gen/SphereField.hpp`). 720 lon * 360 lat cells, SoA: `surfaceElevationM`, `crustThicknessKm`, `continentalFraction`, `plateId`, `convergenceRateRadPerMy`, `crustAgeMy`. Longitude wraps; latitude clamps at the poles.
- **Reference dataset**: `aoc::map::gen::PlateReference` (`include/aoc/map/gen/PlateReference.hpp`). Bird (2003) PB2002 catalog, 52 plates with centroid + composition. Used to seed continental fraction at sim init via nearest-centroid lookup.
- **Plate state**: minimal -- `latDeg`, `lonDeg`, `eulerPoleLatDeg`, `eulerPoleLonDeg`, `angularVelDeg`, `weight`, `landFraction`. All other Plate fields are slated for deletion in Phase 4.
- **Physics passes** (per epoch, in order, on the raster):
  1. ownership: haversine-closest plate per cell -> `plateId`
  2. boundary detection: cell flagged if any 4-neighbour has different `plateId`
  3. closing rate: per-boundary cell, project (vA - vB) onto boundary normal, store positive component into `convergenceRateRadPerMy` (NOT integrated)
  4. thickening: `dCrustKm = K_THICKEN * convergenceRate * dtMy` capped at `maxCrustThicknessKm`. Continental cells only.
  5. subduction: lower-density cell at convergent boundary loses ownership to overrider; `crustThicknessKm` reset; `continentalFraction` set to 0
  6. isostasy: `surfaceElevationM` from Airy compensation given `crustThicknessKm` + `continentalFraction`
  7. erosion: stream-power proportional to relief above sea level
- **Tile assignment**: for each hex tile, `bilinearSample(surfaceElevationM, lat, lon)` then `Mountain` iff `surfaceElevationM > 4000.0f`. Single threshold; no quotas, no percentile, no chain extension.

## OLD Voronoi consumers (P0 audit, 2026-05-06)

375 reference sites across 15 files. Phase column maps to the rip plan.

| File | Hits | Phase |
|---|---:|---|
| src/map/MapGenerator.cpp | 302 | P2-P5 (orogeny scatter + polygon construction + climate shapers + mountain remap) |
| include/aoc/map/gen/Plate.hpp | 35 | P4 (delete 25 OLD fields + SutureSeam) |
| include/aoc/map/HexGrid.hpp | 7 | P4 (delete polygon overlay setters) |
| src/map/gen/PlateIdStash.cpp | 8 | P3 (rewrite haversine-only) |
| src/map/gen/ClimateBiome.cpp | 6 | P5 (delete two-pass + cap + ridge-line + chain extension) |
| src/map/gen/Biogeography.cpp | 3 | P4 (consumer of SutureSeam / boundary types) |
| src/map/gen/PlatePhysics.cpp | 3 | P6 (rewrite to operate on SphereField) |
| include/aoc/map/gen/PlatePhysics.hpp | 1 | P6 |
| src/map/gen/PostSim.cpp | 2 | P4 (delete ophiolite/suture pass) |
| src/map/gen/Thresholds.cpp | 2 | P5 (delete percentile mountainCutoff) |
| include/aoc/map/gen/Thresholds.hpp | 1 | P5 |
| src/tools/MapGenCli.cpp | 2 | P4 (drop --dump-edges et al.) |
| include/aoc/map/MapGenerator.hpp | 1 | P5 (drop Config::mountainRatio) |
| include/aoc/map/gen/MapGenContext.hpp | 1 | P4 |
| include/aoc/map/gen/Noise.hpp | 1 | P6 (no fractalNoise continental seeding) |

To regenerate: `git grep -nE '...the symbol regex...'` (see PR description for the exact regex).

## Citation table (target Phase 6)

| Constant | Value | Source |
|---|---|---|
| `mantleDatumM` | TBD (Phase 6 step 2) | Turcotte & Schubert 2014 ch. 2 |
| `maxCrustThicknessKm` | 70.0 | Tibet observed steady state, Turcotte & Schubert 2014 ch. 4 |
| `K_THICKEN` | TBD (Phase 1 step 4) | Turcotte & Schubert 2014 ch. 6 mass balance |
| `EROSION_K_PER_MY` | TBD (Phase 6 step 2) | Whipple & Tucker 1999 stream-power |
| `MOUNTAIN_THRESHOLD_M` | 4000.0 | Alpine / nival biome floor |
| `MY_PER_EPOCH` | `250 / tectonicEpochs` | Pangaea breakup timescale; Bird 2003 + GPlates |
| Euler-pole jitter `sigma` | TBD (Phase 6 step 4) | GPlates Euler-pole catalog variability |

## Phase tracker

- [x] P0 -- foundation: `SphereField` + `PlateReference` + audit
- [x] P1 -- wire physics on raster behind flag
- [x] P2 -- delete OLD orogeny scatter (~573 LOC removed across P2.1-P2.3e)
- [x] P3 -- haversine ownership + polygon libs deleted (~1875 LOC across P3.1-P3.4)
- [x] P4 -- 22 OLD Plate fields + suture + boundary-polygon-overlay-callers deleted (~590 LOC across P4.1-P4.4); cx/cy + HexGrid setter declarations + member vectors deferred to P6/P7
- [x] P5 -- climate shaping layer deleted (~370 LOC across P5.1-P5.4): mountainRatio quota, Thresholds percentile, ClimateBiome 2-pass + 12% cap + ridge-line + chain extension, MOUNTAIN_BASE_M/SCALE_M remap, glacial rebound bonus. Mountain status now `surfaceElevationM > 4000m` single threshold.
- [ ] P6 -- physics correctness + cited constants
  - [x] P6.1 -- delete legacy per-plate PhysicsGrid pipeline callers in MapGenerator.cpp
  - [x] P6.2 -- mantleDatumM 2900→3549 (Turcotte & Schubert 2014 ch. 2 rederivation)
  - [x] P6.3 -- maxCrustThicknessKm 65→70 (Tibet observed ceiling, T&S 2014 ch. 4)
  - [x] P6.4 -- K_EROSION_PER_MY 0.05→0.06 (Whipple & Tucker 1999 + Wilkinson & McElroy 2007)
  - [x] P6.5 -- continental seeding: Bird 2003 K-nearest linear falloff (R=0.55 rad) replaces hard step function
  - [x] P6.6 -- plate-extent gate in accumulateClosingRate (sqrt(weight)-scaled angular reach 0.6 rad/√w)
  - [x] P6.7 -- proper subduction: lower-density-side ownership transfer + 50 km cell-width gate (already in applySubduction)
  - [x] P6.8 -- MY_PER_EPOCH = 250/EPOCHS (already in MapGenerator.cpp; legacy 1.0 dead post-P6.1)
  - [x] P6.9 -- angularVelDeg floors deleted; ranges now Earth (HS3-NUVEL-1A): 0.10-0.70 land / 0.30-1.10 ocean deg/Ma
  - [x] P6.10 -- stochastic Euler-pole jitter (σ scales as √dt; 10%/Myr velocity, 1°/Myr pole drift; Müller et al. 2008)
- [x] P7 -- audit matrix + freeze
  - 21-sim sweep (4/6/8 players × 1000 turns × 7 seeds, continents): 0 crashes; victory mix CULTURE 14.3% / RELIGION 9.5% / DOMINATION 4.8% / SCORE 71.4%; 7444 routes / 1108 districts / 172 wonders. Physics rewrite preserves balance.
  - Obsolete docs archived to `docs/archive/`: SPHERE_MIGRATION.md, PHYSICS_REWRITE_CONTINUE.md.

## Open debt — cleanup pass 2026-05-06

Status update after the post-P7 cleanup pass:

- ~~`PhysicsGrid` struct + helpers~~ DELETED. `PlatePhysics.cpp` removed entirely; `PlatePhysics.hpp` slimmed to just `PhysicsConstants`. `Plate::grid` member, all 5 `initialisePlatePhysicsGrid` callers, `AOC_PHYSICS_DEBUG` block, `--dump-physics-cells` CLI feature + writer + `Config::physicsCellDumpPath` all gone.
- ~~`HexGrid` dead plate setters/getters~~ DELETED — `setPlateLatLon`/`Weight`/`EulerPole`/`AngularVelDeg`/`Rot` + `setPlatePolygons`/`PolygonEdgeTypes`/`PolygonNeighborIds` and matching getters + member vectors purged. Live ones kept (`setPlateMotions`/`Centers`/`LandFrac`/`CrustAge`/`MergesAbsorbed`/`IsPolar` — IceAndRock + Biogeography + EarthSystem + ClimateBiome consume the getters).
- ~~Diagnostic CLI dumps~~ DELETED — `--dump-plates`, `--dump-edges`, `--dump-mountain-edges` writers + flag parsing in `MapGenCli.cpp` (consumed deleted Voronoi polygon overlay state).
- ~~`isMountainTile` vector + foothill BFS~~ DELETED in `ClimateBiome.cpp`. `mountainDist` kept zero-filled so the foothill-belt branch keeps compiling (collapses to `hillChance == 0`).
- ~~Dead 2D `cx/cy` motion blocks~~ DELETED in `MapGenerator.cpp`: legacy wrap/clamp loop + 2D polar-wandering rotation (latter never updated lat/lon, was overwritten by next Mollweide forward).
- ~~Empty per-epoch crust-age advance loop~~ DELETED in `MapGenerator.cpp` (loop body was empty post-P4.3h-c-5).
- ~~PostSim cx/cy boundary-normal computation~~ DELETED — result unused (vx/vy gone in P4.3h-a).
- ~~`ophioliteMask` always-zero branches~~ DELETED in PostSim Pass 6 + IceAndRock rt=3 path. Vector + param signature retained for ABI stability.
- ~~Tombstone-comment cleanup~~ Pass 2: down to 2 (legitimate) tombstones in MapGenerator.cpp (from 128 originally). Removed dead hotspot-trail per-plate ownership lookup loop, leading-margin-factor=0 dead branch, slab-rebound stamp comment, oceanWedge override / mid-ocean ridge bathymetry tombstones, MOUNTAIN_BASE_M/SCALE_M references, Wilson crust accounting block, microplateCount cap comment, hotspotTrail accumulation comment, glacial isostatic rebound comment, aspect oscillation block, failed-rift scar comment, terrane accretion narrative, slab-pull/Wilson scan tombstone, --dump-physics-cells tombstone, plate-pair velocity coupling tombstone. MapGenerator.cpp shrunk 2635→2446 lines (~190 LOC of dead comments).

### Still open (multi-session refactors)

- `Plate::cx`, `Plate::cy`: 11 live refs remain across init seeds, hotspot proximity check, rift seeding, Mollweide derive, and `setPlateCenters` consumer (EarthSystem boundary-normal pass). Removing these requires rewriting hotspot/rift placement + EarthSystem to use lat/lon — multi-session blast radius.
- `PlateBoundary.hpp`: actively used by `Resources.cpp` (`BoundaryType` enum drives strategic resource placement) — not deletable.
