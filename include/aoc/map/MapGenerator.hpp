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

namespace aoc::map {

/// Map generation type controlling landmass shape.
///
/// 2026-05-03: per user direction, only `Continents` is supported. All other
/// map types are commented-out below so the enum value range stays stable for
/// any save-game blob that still serializes a non-zero value (it will be
/// remapped to Continents at load time). Their generator code paths have
/// been deleted entirely.
enum class MapType : uint8_t {
    Continents = 0,         ///< Tectonic-plate-simulated continents. Only supported type.
    // Islands,                ///< [removed 2026-05-03]
    // ContinentsPlusIslands,  ///< [removed 2026-05-03]
    // LandOnly,               ///< [removed 2026-05-03] (was "pangaea" CLI alias)
    // LandWithSeas,           ///< [removed 2026-05-03]
    // Fractal,                ///< [removed 2026-05-03]
};

/// Predefined map sizes.
enum class MapSize : uint8_t {
    Small,     ///< 100x66
    Standard,  ///< 140x90
    Large,     ///< 200x130
    Huge,      ///< 280x180
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

/// Get dimensions for a given MapSize preset.
[[nodiscard]] constexpr std::pair<int32_t, int32_t> mapSizeDimensions(MapSize size) {
    switch (size) {
        case MapSize::Small:    return {100, 66};
        case MapSize::Standard: return {140, 90};
        case MapSize::Large:    return {200, 130};
        case MapSize::Huge:     return {280, 180};
        default:                return {140, 90};
    }
}

class MapGenerator {
public:
    struct Config {
        int32_t  width     = 140;
        int32_t  height    = 90;
        uint64_t seed      = 42;
        float    forestRatio   = 0.25f;
        float    hillRatio     = 0.18f;
        MapType     mapType   = MapType::Continents;  ///< Landmass generation style
        MapSize     mapSize   = MapSize::Standard;     ///< Preset size (overrides width/height when applied)
        MapTopology topology  = MapTopology::Flat;     ///< Grid topology (Flat or Cylindrical)
        /// Sphere → rectangle projection used to map hex tiles back to
        /// lat/lon when sampling SphereField state. Default Mollweide
        /// (equal-area, ellipse footprint, polar voids in corners).
        /// See `aoc::map::gen::MapProjection` for alternatives
        /// (Equirectangular, Mercator, Robinson) — selectable from
        /// the Continent Creator UI.
        gen::MapProjection projection = gen::MapProjection::Mollweide;
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
        int32_t  tectonicTotalMy = 0;
        /// Internal: epoch count derived from `tectonicTotalMy`. Set
        /// only if you need to override directly (legacy paths).
        int32_t  tectonicEpochs = 0;
        /// `landPlateCount` caps the initial continental plate seeds.
        /// 0 = use Müller 2022-derived statistics-driven count.
        int32_t  landPlateCount = 0;
        /// Stepper hook: if >0, the sim halts after this many epochs
        /// have run. Used by the Continent Creator scrubber to render
        /// intermediate states. 0 = run the full sim.
        int32_t  runEpochsLimit = 0;
        // DEBT: driftFraction is dead since the legacy 2D plate motion
        // was deleted (2026-07-05 phase 1) — raster advection uses the
        // physical Euler-pole velocities directly. Field + UI slider
        // kept this phase to avoid unplanned UI surgery; remove both
        // with the phase-7 unification cleanup.
        float    driftFraction = 0.0f;

        // ----- TEMPORAL CLIMATE PHASES (single-snapshot proxies) -----
        // The map gen runs once at world creation, but real Earth has
        // gone through climatic phases (greenhouse, icehouse, glacial)
        // and orbital cycles (Milankovitch, ENSO). These knobs select a
        // SNAPSHOT of those phases to apply at generation time.
        //
        // climatePhase: 0 = neutral, 1 = greenhouse (warmer, less ice,
        //   higher sea level), 2 = icehouse (colder, expanded ice,
        //   lower sea level — Pleistocene maximum).
        int32_t  climatePhase = 0;
        // seaLevelDelta: creative sea-level slider, applied as a shift
        // of the final land/water cut in Thresholds (one unit = 1000 m
        // of stand). The physical sea level itself is solved from the
        // conserved ocean volume each epoch (SphereField::seaLevelM);
        // the climate phase perturbs that volume.
        float    seaLevelDelta = 0.0f;
        // axialTilt in degrees (Earth = 23.5°). Higher = stronger
        // seasonal contrast (wider tropic + polar zones, less temperate).
        // 0 = uniform-temperate world.
        float    axialTilt = 23.5f;
        // ensoState: 0 = neutral, 1 = El Niño (Pacific-east warm/wet,
        // west dry), 2 = La Niña (opposite). Affects equatorial moisture.
        int32_t  ensoState = 0;
        // milankovitchPhase: 0..1 — orbital eccentricity cycle position.
        // 0 = circular (mild seasons), 1 = elliptical (extreme).
        float    milankovitchPhase = 0.0f;

    };

    /// Substep size in millions of years per simulation epoch. Total
    /// sim time = MY_PER_EPOCH_TARGET * tectonicEpochs; 60 epochs covers
    /// ~3 Gy of geological time (default supercontinent-cycle target).
    inline static constexpr int32_t MY_PER_EPOCH_TARGET = 50;
    inline static constexpr int32_t DEFAULT_TECTONIC_TOTAL_MY = 3000;

    /**
     * @brief Generate a complete hex map.
     * @param config Generation parameters.
     * @param outGrid Output grid (will be initialized to config dimensions).
     */
    static void generate(const Config& config, HexGrid& outGrid);

private:
    /// Value noise sampled at (x, y) with a given frequency and PRNG.
    static float noise2D(float x, float y, float frequency, aoc::Random& rng);

    /// Multi-octave fractal noise for natural-looking terrain.
    static float fractalNoise(float x, float y, int octaves, float frequency,
                              float persistence, aoc::Random& rng);

    static void assignTerrain(const Config& config, HexGrid& grid, aoc::Random& rng);
    static void assignFeatures(const Config& config, HexGrid& grid, aoc::Random& rng);
    static void generateRivers(HexGrid& grid, aoc::Random& rng);
    static void smoothCoastlines(HexGrid& grid);
    static void placeNaturalWonders(HexGrid& grid, aoc::Random& rng);

    // 2026-05-03: generateRealisticTerrain removed (was the LandWithSeas
    // entry point; LandWithSeas is gone).

    /// Place resources based on geology zones (Realistic map type).
    static void placeGeologyResources(const Config& config, HexGrid& grid, aoc::Random& rng);

    /// Place resources using simple terrain-based rules (non-Realistic map types).
    static void placeBasicResources(const Config& config, HexGrid& grid, aoc::Random& rng);

    /// Uniform per-tile probability, geology-blind.  For Random placement mode.
    static void placeRandomResources(const Config& config, HexGrid& grid, aoc::Random& rng);

    /// Re-distribute strategic resources so each large landmass quadrant gets
    /// comparable coverage.  For Fair placement mode.  Applied on top of
    /// geology placement: we keep the geology-selected positions but remove
    /// surplus from over-served quadrants and add deficits to under-served
    /// ones.
    static void balanceResourcesFair(const Config& config, HexGrid& grid, aoc::Random& rng);
};

} // namespace aoc::map
