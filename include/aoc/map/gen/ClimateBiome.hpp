#pragma once

/**
 * @file ClimateBiome.hpp
 * @brief 2-D climate model + biome assignment + Hills lifecycle.
 *
 * Walks every tile, runs Hadley + ocean-current + monsoon + ENSO +
 * continentality + orographic + rain-shadow models to produce
 * temperature/moisture, then maps to TerrainType. Mountain assignment is
 * driven by orogeny percentile rather than raw elevation.
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

void runClimateBiomePass(HexGrid& grid,
                         const MapGenerator::Config& config,
                         aoc::Random& rng,
                         const std::vector<float>& elevationMap,
                         const std::vector<float>& mountainElev,
                         const std::vector<int32_t>& distFromCoast,
                         const std::vector<float>& orogeny,
                         float waterThreshold,
                         float mountainThreshold);

} // namespace gen
} // namespace aoc::map
