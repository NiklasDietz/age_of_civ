#pragma once

/**
 * @file ClimateBiome.hpp
 * @brief 2-D climate model + biome assignment + Hills lifecycle.
 *
 * Walks every tile, runs Hadley + ocean-current + monsoon + ENSO +
 * continentality + orographic + rain-shadow models to produce a mean annual
 * temperature (degrees Celsius) and a 0-1 moisture index, then maps the pair to
 * TerrainType through real climatological isotherms. Mountains are relabelled
 * from the upstream orogeny mask, not decided here.
 *
 * Behaviour-preserving extraction from src/map/MapGenerator.cpp on 2026-05-03.
 */

#include "aoc/core/Random.hpp"
#include "aoc/map/MapGenerator.hpp"

#include <cstdint>
#include <vector>

namespace aoc::map {

class HexGrid;

namespace gen {

// ============================================================================
// Zonal climate profiles (pure functions of latitude)
// ============================================================================
//
// Exposed for unit testing. These two functions ARE the climate calibration:
// the biome bands are wherever they cross the isotherms below, so a test that
// pins their values is what stops a future change to the latitude source from
// silently relocating every biome -- which is exactly how Grassland ended up
// above 66 degrees and Tundra fell to 0.1 % of land.

/// Biome isotherms, mean annual temperature in Celsius. See ClimateBiome.cpp
/// for the derivation and the latitude each one lands at.
constexpr float ISO_PERMANENT_ICE_C = -15.0f;
constexpr float ISO_TREE_LINE_C     = -7.0f;
constexpr float ISO_TEMPERATE_C     = 2.0f;
constexpr float ISO_SUBTROPICAL_C   = 15.0f;

/// Zonal mean annual near-surface air temperature at sea level, Celsius, for an
/// Earth-obliquity planet. Sign of `latDeg` is ignored.
[[nodiscard]] float zonalMeanAnnualTempC(float latDeg);

/// Zonal mean moisture as a 0-1 index, following the Hadley / Ferrel / polar
/// cell structure. Sign of `latDeg` is ignored.
[[nodiscard]] float zonalMoisture(float latDeg);

void runClimateBiomePass(HexGrid& grid, const MapGenerator::Config& config, aoc::Random& rng,
                         const std::vector<float>& elevationMap,
                         const std::vector<int32_t>& distFromCoast,
                         const std::vector<float>& orogeny, const std::vector<uint8_t>& isWater,
                         float waterThreshold);

} // namespace gen
} // namespace aoc::map
