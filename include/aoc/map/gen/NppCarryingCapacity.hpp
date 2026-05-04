#pragma once

/**
 * @file Session15.hpp
 * @brief SESSION 15 -- NPP / growing season / frost / carrying capacity /
 *        soil texture / temp ranges / UV / coral bleach / magnetic anomaly /
 *        heat flow / volcano return / tsunami runup.
 *
 * Behaviour-preserving extraction from src/map/MapGenerator.cpp on 2026-05-03.
 */

#include <cstdint>
#include <vector>

namespace aoc::map {

class HexGrid;

namespace gen {

/// Run SESSION 15 analytics. `soilFert` carries soil-fertility per tile from
/// the earlier EARTH-SYSTEM POST-PASSES preamble (used to scale NPP).
void runNppCarryingCapacity(HexGrid& grid, bool cylindrical,
                  const std::vector<float>& soilFert);

} // namespace gen
} // namespace aoc::map
