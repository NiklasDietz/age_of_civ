#pragma once

/**
 * @file Relief.hpp
 * @brief Local topographic relief from the hex elevation field.
 *
 * Shared by the Hills criterion (gen/ClimateBiome.cpp) and the
 * AOC_DUMP_OROGENY diagnostic (MapGenerator.cpp), which is the whole point:
 * the diagnostic that justifies a relief threshold must measure the same
 * quantity the criterion tests, or the calibration is against a different
 * number than the one in production.
 *
 * Relief is reported per 100 km of GROUND distance, not per tile, because tile
 * spacing is neither constant nor isotropic: under Lambert equal-area at 140x90
 * an equatorial tile is 286 km east-west by 142 km north-south, and by 75 deg
 * that is 74 km by 547 km. A per-tile gradient would therefore read as a
 * function of latitude and map width rather than of terrain -- the same class of
 * bug that made `axis_aligned_frac` meaningless until it was measured in sphere
 * tangent space.
 */

#include <cstdint>
#include <vector>

namespace aoc::map {

class HexGrid;

namespace gen {

/// Ground distance from a tile to its hex neighbours, in km, per axis.
/// Derived from the grid's own row latitudes, so it follows whichever
/// projection populated them without needing per-projection algebra.
struct TileSpacingKm {
    float eastWest;
    float northSouth;
};
[[nodiscard]] TileSpacingKm tileSpacingKm(const HexGrid& grid, int32_t row);

/// Maximum local relief around tile (col, row), in metres per 100 km of ground
/// distance, taken over the six hex neighbours of the unitless `elevationMap`
/// (one unit = 5000 m; see MapGenerator's world-frame elevation sample).
///
/// Water neighbours are skipped. The continental slope down to the abyss is not
/// terrain roughness, and counting it would make every coastal tile the steepest
/// place on the map. A tile with no land neighbour returns 0.
[[nodiscard]] float localReliefMPer100Km(const HexGrid& grid,
                                         const std::vector<float>& elevationMap,
                                         float waterThreshold, int32_t col, int32_t row);

} // namespace gen
} // namespace aoc::map
