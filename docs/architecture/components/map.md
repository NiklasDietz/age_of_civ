# Component: map

## Responsibility

Stores the hex tile grid and provides all geometry, terrain, pathfinding, fog of war, and
the full procedural map generator — including the plate-tectonics worldgen pipeline.

## Key files

- [include/aoc/map/HexGrid.hpp](../../../include/aoc/map/HexGrid.hpp) — `HexGrid`: flat
  SoA arrays indexed by `row * width + col` (odd-r offset coords). Each property
  (`terrain`, `elevation`, `improvement`, `resource`, `owner`, `chokepointType`, …) is a
  separate contiguous `std::vector` for cache-friendly single-property sweeps. Supports
  two topologies: `Flat` (bounded rectangle) and `Cylindrical` (east-west wrap). Grid
  width/height up to 280×180 for the Huge preset.
- [include/aoc/map/HexCoord.hpp](../../../include/aoc/map/HexCoord.hpp) — `HexCoord` /
  `AxialCoord`: offset-to-axial conversion, neighbor enumeration (6 directions), distance,
  ring iteration.
- [include/aoc/map/Terrain.hpp](../../../include/aoc/map/Terrain.hpp) — `TerrainType`
  enum and yield tables for food/production/gold per terrain.
- [include/aoc/map/MapGenerator.hpp](../../../include/aoc/map/MapGenerator.hpp) —
  `MapGenerator`: top-level entry point. `Config` struct holds width/height/seed/water
  ratio/forest ratio/hill ratio/`MapType`/`MapSize`/`ResourcePlacementMode`. The only
  active `MapType` is `Continents` (others removed 2026-05-03 but kept as `#if 0`
  stubs for reference). Four `MapSize` presets: Small (100×66), Standard (140×90),
  Large (200×130), Huge (280×180).
- [include/aoc/map/Pathfinding.hpp](../../../include/aoc/map/Pathfinding.hpp) —
  A\* pathfinding over the hex grid, movement cost per terrain/improvement type.
- [include/aoc/map/FogOfWar.hpp](../../../include/aoc/map/FogOfWar.hpp) — per-player
  visibility and explored bitsets.
- [include/aoc/map/RiverGameplay.hpp](../../../include/aoc/map/RiverGameplay.hpp) —
  river adjacency effects on combat (crossing penalty, `RIVER_DEFENSE_BONUS`).

### Map generator pipeline (`src/map/gen/` + `include/aoc/map/gen/`)

The generator runs a physics-inspired pipeline on a Mollweide-projected sphere:

- [SphereGeometry.hpp](../../../include/aoc/map/gen/SphereGeometry.hpp) — lat/lon ↔
  unit-vector conversions, haversine distance, Euler-pole rotation, Mollweide forward +
  inverse projection.
- `Plate.hpp` / `PlatePhysics.hpp` / `PlateBoundary.hpp` — tectonic plate objects,
  Euler-pole motion, convergent/divergent/transform boundary classification.
- `SphereField.hpp` / `SphereFieldPhysics.hpp` — scalar fields on the sphere for
  elevation and atmospheric data.
- `AtmosphereOcean.hpp` — atmospheric circulation, ocean currents, precipitation.
- `ClimateBiome.hpp` / `KoppenStructures.hpp` — Köppen climate classification → biome
  assignment.
- `Rivers.hpp` / `StreamRiparian.hpp` / `LakePurge.hpp` — river network generation
  from drainage basins, riparian vegetation.
- `Biogeography.hpp` / `BiomeSubtypes.hpp` / `EarthSystem.hpp` — biome distribution,
  vegetation placement.
- `Resources.hpp` — geology-driven strategic/luxury/bonus resource placement;
  `ResourcePlacementMode::Realistic` uses lithology and subduction boundaries.
- `Lithology.hpp` — rock type classification per plate boundary type.
- `InsolationSlope.hpp` — slope-based solar radiation for crop potential.
- `NppCarryingCapacity.hpp` — net primary productivity → barbarian/starting city
  carrying capacity.
- `PostSim.hpp` — post-simulation cleanup: coastal landforms, cliff coasts, ice caps,
  chokepoint classification.
- `EcoAnalytics.hpp` — diagnostic statistics for the generator (tile counts, biome
  balance), consumed by `aoc_mapgen` and the Python analysis tools in `tools/plate_data/`.

## Public surface

- `HexGrid` — read/written by simulation (improvements, ownership, resources), render
  (terrain sprites), save/load, and the map generator.
- `MapGenerator::generate(config, rng)` — called by `GameServer::initialize()` and
  `aoc_mapgen`.
- `Pathfinding::findPath(grid, from, to, player)` — used by unit movement (simulation)
  and AI movement planning.
- `HexCoord` / `AxialCoord` — used by every subsystem that reasons about tile positions.

## Internal structure

Top-level files handle grid storage, geometry, and gameplay mechanics. The `gen/`
subdirectory is the worldgen pipeline: ~25 files organized as sequential stages
(`Plate*` → `Atmosphere*` → `Climate*` → `Rivers` → `Resources` → `PostSim`). Each
stage has its own header/source pair and operates on a shared `MapGenContext` passed
by reference.
