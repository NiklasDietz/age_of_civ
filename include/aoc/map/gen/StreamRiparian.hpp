#pragma once

/**
 * @file Session14.hpp
 * @brief SESSION 14 -- stream order / navigability / dam site / riparian /
 *        aquifer recharge / per-crop suitability (8) / pasture / forestry /
 *        fold axis / metamorphic facies / plate stress / cyclone intensity /
 *        drought severity / storm wave height / snow line / habitat
 *        fragmentation / endemism / species richness.
 *
 * Behaviour-preserving extraction from src/map/MapGenerator.cpp on 2026-05-03.
 */

#include <cstdint>
#include <vector>

namespace aoc::map {

class HexGrid;

namespace gen {

/// Run SESSION 14 analytics.
void runStreamRiparian(HexGrid& grid, bool cylindrical,
                  const std::vector<float>& soilFert,
                  const std::vector<float>& orogeny);

} // namespace gen
} // namespace aoc::map
