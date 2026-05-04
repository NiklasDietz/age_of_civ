#pragma once

/**
 * @file Session16.hpp
 * @brief SESSION 16 -- TPI / TWI / roughness / curvature / drainage / livestock
 *        suitability / fault traces / reef terraces / mine suit / coal seam /
 *        soil pH / ice cover / hydropower for Continents maps.
 *
 * Behaviour-preserving extraction from src/map/MapGenerator.cpp on 2026-05-03.
 */

#include <cstdint>
#include <vector>

namespace aoc::map {

class HexGrid;

namespace gen {

/// Run SESSION 16 analytics. Inputs read from grid getters and from the
/// outer-scope vectors `soilFert`, `sediment`, `lakeFlag` populated earlier
/// in the pipeline. Outputs stored via grid.set* methods.
void runDrainageLivestock(HexGrid& grid, bool cylindrical,
                  const std::vector<float>& soilFert,
                  const std::vector<float>& sediment,
                  const std::vector<uint8_t>& lakeFlag);

} // namespace gen
} // namespace aoc::map
