#pragma once

/**
 * @file MapGenerator.hpp
 * @brief Procedural hex map generation using layered noise.
 *
 * Generates terrain, features, elevation, and rivers for a hex grid.
 * Fully deterministic given the same seed.
 */

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/gen/SphereGeometry.hpp"
#include "aoc/core/Random.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace aoc::map {

/// Map generation type controlling landmass shape.
///
/// 2026-05-03: per user direction, only `Continents` is supported. All other
/// map types are commented-out below so the enum value range stays stable for
/// any save-game blob that still serializes a non-zero value (it will be
/// remapped to Continents at load time). Their generator code paths have
/// been deleted entirely.
enum class MapType : uint8_t {
    Continents = 0, ///< Tectonic-plate-simulated continents. Only supported type.
    // Islands,                ///< [removed 2026-05-03]
    // ContinentsPlusIslands,  ///< [removed 2026-05-03]
    // LandOnly,               ///< [removed 2026-05-03] (was "pangaea" CLI alias)
    // LandWithSeas,           ///< [removed 2026-05-03]
    // Fractal,                ///< [removed 2026-05-03]
};

/// Predefined map sizes.
enum class MapSize : uint8_t {
    Small,    ///< 100x66
    Standard, ///< 140x90
    Large,    ///< 200x130
    Huge,     ///< 280x180
};

/// Resource placement policy.
///   Realistic: geology-driven — coal in sedimentary basins, copper/iron on
///              continental shield, oil near subduction boundaries, uranium
///              near fault lines.  (Current default.)
///   Fair:      stratified per player spawn — every civ gets roughly equal
///              strategic-resource access within their starting area.
///   Random:    uniform per-tile chance; ignore geology layer entirely.
enum class ResourcePlacementMode : uint8_t {
    Realistic = 0,
    Fair      = 1,
    Random    = 2,
};

/// Share of the luxury types each start region is denied under Fair and
/// Random placement, so no civ has everything and neighbours complement.
inline constexpr float REGION_DENIED_FRACTION = 0.4f;
/// Tiles every luxury type keeps on the map after the regional pass.
inline constexpr int32_t LUXURY_MIN_TILES = 2;
/// Luxury types every start has within reach after the regional pass, so a
/// civ starts with something to sell as well as something to lack.
inline constexpr int32_t REGION_MIN_LUXURY_TYPES = 3;

/// Get dimensions for a given MapSize preset.
[[nodiscard]] constexpr std::pair<int32_t, int32_t> mapSizeDimensions(MapSize size) {
    switch (size) {
    case MapSize::Small:
        return {100, 66};
    case MapSize::Standard:
        return {140, 90};
    case MapSize::Large:
        return {200, 130};
    case MapSize::Huge:
        return {280, 180};
    default:
        return {140, 90};
    }
}

class MapGenerator {
public:
    struct Config {
        int32_t width     = 140;
        int32_t height    = 90;
        uint64_t seed     = 42;
        float forestRatio = 0.25f;
        float hillRatio   = 0.18f;
        MapType mapType   = MapType::Continents; ///< Landmass generation style
        MapSize mapSize = MapSize::Standard; ///< Preset size (overrides width/height when applied)
        /// Grid topology. Cylindrical (east-west wrap) is what the shipping
        /// game always uses (src/app/Application.cpp), so it is the default:
        /// a Flat default meant every headless run, every metrics sweep and
        /// the multiplayer server generated a topology nobody plays, and a
        /// full-width ocean-stripe bug survived an entire phase programme
        /// because the harness could not see it (commit 9debafd). Flat remains
        /// selectable for tests that specifically want a non-wrapping grid.
        MapTopology topology = MapTopology::Cylindrical;
        /// Sphere → rectangle projection used to map hex tiles back to
        /// lat/lon when sampling SphereField state.
        ///
        /// Default Lambert cylindrical equal-area, because it is the only
        /// option under which a tile COUNT is proportional to planet AREA --
        /// so land fraction and component sizes mean what they appear to mean.
        /// It also fills the rectangle (Mollweide, the previous default,
        /// leaves ~21.4 % of the grid outside its ellipse and those tiles are
        /// force-set to ocean, which additionally makes polar continents
        /// unrepresentable) and wraps validly at every latitude, which
        /// Mollweide does not.
        ///
        /// See `aoc::map::gen::MapProjection` for the alternatives, all
        /// selectable from the Continent Creator UI.
        gen::MapProjection projection   = gen::MapProjection::LambertCylindricalEqualArea;
        ResourcePlacementMode placement = ResourcePlacementMode::Realistic;
        /// Continents-only knobs.
        ///
        /// `tectonicTotalMy` sets the total simulated geological time
        /// in millions of years. Default 3000 My (3 Gy) covers ~5
        /// Wilson supercontinent cycles (Anderson 2007 estimates the
        /// average cycle period at ~500-700 My). 0 = use built-in
        /// default. The generator converts internally:
        ///   EPOCHS = round(tectonicTotalMy / MY_PER_EPOCH_TARGET)
        /// where MY_PER_EPOCH_TARGET = 50 My (substep size; large enough
        /// to keep sim fast, small enough that the slope-based stream-power
        /// erosion (K_EROSION_M_PER_MY_PER_SLOPE, SphereFieldPhysics.cpp)
        /// stays well-conditioned per step).
        int32_t tectonicTotalMy = 0;
        /// Internal: epoch count derived from `tectonicTotalMy`. Set
        /// only if you need to override directly (legacy paths).
        int32_t tectonicEpochs = 0;
        /// `landPlateCount` caps the initial continental plate seeds.
        /// 0 = use Müller 2022-derived statistics-driven count.
        int32_t landPlateCount = 0;
        /// Stepper hook: if >0, the sim halts after this many epochs
        /// have run. Used by the Continent Creator scrubber to render
        /// intermediate states. 0 = run the full sim.
        int32_t runEpochsLimit = 0;
        // DEBT: driftFraction is dead since the legacy 2D plate motion
        // was deleted (2026-07-05 phase 1) — raster advection uses the
        // physical Euler-pole velocities directly. Field + UI slider
        // kept this phase to avoid unplanned UI surgery; remove both
        // with the phase-7 unification cleanup.
        float driftFraction = 0.0f;

        // ----- TEMPORAL CLIMATE PHASES (single-snapshot proxies) -----
        // The map gen runs once at world creation, but real Earth has
        // gone through climatic phases (greenhouse, icehouse, glacial)
        // and orbital cycles (Milankovitch, ENSO). These knobs select a
        // SNAPSHOT of those phases to apply at generation time.
        //
        // climatePhase: 0 = neutral, 1 = greenhouse (warmer, less ice,
        //   higher sea level), 2 = icehouse (colder, expanded ice,
        //   lower sea level — Pleistocene maximum).
        int32_t climatePhase = 0;
        // seaLevelDelta: creative sea-level slider, applied as a shift
        // of the final land/water cut in Thresholds (one unit = 1000 m
        // of stand). The physical sea level itself is solved from the
        // conserved ocean volume each epoch (SphereField::seaLevelM);
        // the climate phase perturbs that volume.
        float seaLevelDelta = 0.0f;
        // axialTilt in degrees (Earth = 23.5°). Higher = stronger
        // seasonal contrast (wider tropic + polar zones, less temperate).
        // 0 = uniform-temperate world.
        float axialTilt = 23.5f;
        // ensoState: 0 = neutral, 1 = El Niño (Pacific-east warm/wet,
        // west dry), 2 = La Niña (opposite). Affects equatorial moisture.
        int32_t ensoState = 0;
        // milankovitchPhase: 0..1 — orbital eccentricity cycle position.
        // 0 = circular (mild seasons), 1 = elliptical (extreme).
        float milankovitchPhase = 0.0f;
    };

    /// Substep size in millions of years per simulation epoch. Total
    /// sim time = MY_PER_EPOCH_TARGET * tectonicEpochs; 60 epochs covers
    /// ~3 Gy of geological time (default supercontinent-cycle target).
    inline static constexpr int32_t MY_PER_EPOCH_TARGET       = 50;
    inline static constexpr int32_t DEFAULT_TECTONIC_TOTAL_MY = 3000;

    /**
     * @brief Generate a complete hex map.
     * @param config Generation parameters.
     * @param outGrid Output grid (will be initialized to config dimensions).
     */
    static void generate(const Config& config, HexGrid& outGrid);

private:
    // The noise2D / fractalNoise forwarders were removed on 2026-07-27. They
    // added nothing over aoc::map::gen::noise2D / fractalNoise (see
    // gen/Noise.hpp), and the only reason gen/Features.cpp reached them was
    // unqualified name lookup inside a member function; it now says
    // `using gen::fractalNoise;` alongside the hashNoise / lerp / smoothstep
    // using-declarations it already had.

    /// Fields `assignTerrain` produces that later passes need but the HexGrid
    /// cannot carry. `HexGrid::elevation()` is an int8 display tier (-1..3), so
    /// anything wanting real depth -- the continental-shelf cut, local relief --
    /// has to be handed the metric field explicitly. Threading it through the
    /// pass signatures is deliberate: adding another parallel vector to HexGrid
    /// would drag in initialize() clearing, the Serializer and the save format
    /// for a value that is regenerated from scratch every time.
    struct TerrainFields {
        /// Unitless surface elevation, `surfaceElevationM / 5000`, relative to
        /// the mantle datum. Multiply by 5000 for metres.
        std::vector<float> elevationMap;
        /// Crustal composition per tile, 0 = pure oceanic, 1 = pure
        /// continental, sampled from SphereField::continentalFraction.
        std::vector<float> continentalFraction;
        /// Fraction of the tile's raster footprint (0..1) that is water
        /// shallower than the 140 m shelf break, counted per sub-sample BEFORE
        /// they are averaged into elevationMap.
        ///
        /// Why this exists separately from elevationMap. The tile elevation is
        /// the MEAN of a 4x4 sub-sample grid, and the shelf tier is then a
        /// threshold on that mean. On a concave margin -- terrace at -90 m
        /// falling to a slope foot at -2200 m -- averaging then thresholding is
        /// a biased estimator of shelf AREA: a tile half terrace and half slope
        /// averages far below the 140 m cut and reports zero shelf instead of
        /// one half. Measured consequence: at SHELF_CELLS=12 the raster carried
        /// shelf/planet 0.053, inside the Earth band, while the hex map read
        /// 0.024 and the gate scored 0/24. Thresholding each sub-sample first
        /// and then averaging is unbiased.
        ///
        /// Reported as a diagnostic only -- it does NOT feed the terrain tier or
        /// the gate, so the existing measurement keeps its meaning and the two
        /// estimators can be compared on the same run. Empty when the generator
        /// did not run the sphere path.
        std::vector<float> shelfSubgridFraction;
        /// Elevation value that separates land from water (the sea-level cut).
        float waterThreshold = 0.0f;
    };

    static void assignTerrain(const Config& config, HexGrid& grid, aoc::Random& rng,
                              TerrainFields& outFields);
    static void assignFeatures(const Config& config, HexGrid& grid, aoc::Random& rng);
    static void generateRivers(HexGrid& grid, aoc::Random& rng);
    static void smoothCoastlines(HexGrid& grid, const TerrainFields& fields);
    static void placeNaturalWonders(HexGrid& grid, aoc::Random& rng);

    // 2026-05-03: generateRealisticTerrain removed (was the LandWithSeas
    // entry point; LandWithSeas is gone).

    /// Place resources based on geology zones (Realistic map type).
    static void placeGeologyResources(const Config& config, HexGrid& grid, aoc::Random& rng);

    /// Place resources using simple terrain-based rules (non-Realistic map types).
    static void placeBasicResources(const Config& config, HexGrid& grid, aoc::Random& rng);

    /// Uniform per-tile probability, geology-blind.  For Random placement mode.
    static void placeRandomResources(const Config& config, HexGrid& grid, aoc::Random& rng);

public:
    /// Regional exclusivity for Fair and Random placement, run once the
    /// starts are known (they are chosen after generate). Land is split into
    /// nearest-start regions; the six balanced strategics are spread so each
    /// region holds a comparable share; each region is denied
    /// REGION_DENIED_FRACTION of the luxury types (consecutive windows of one
    /// shuffled order, so no type is denied everywhere and neighbouring
    /// regions complement each other), its tiles of a denied type are swapped
    /// to an allowed luxury of the same climate band or cleared, every type
    /// keeps LUXURY_MIN_TILES tiles, and every start has REGION_MIN_LUXURY_TYPES
    /// allowed types within RESOURCE_REACH_RADIUS. Realistic placement returns
    /// at once: there, scarcity is worldgen's job.
    static void balanceResourcesFair(HexGrid& grid, const std::vector<hex::AxialCoord>& starts,
                                     ResourcePlacementMode placement, aoc::Random& rng);

    /// Nearest start per tile (wrap-aware); -1 for water and impassable tiles.
    [[nodiscard]] static std::vector<int32_t> startRegions(const HexGrid& grid,
                                                           const std::vector<hex::AxialCoord>& starts);
};

} // namespace aoc::map
