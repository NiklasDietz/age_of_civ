#pragma once

/**
 * @file LandmassMetrics.hpp
 * @brief Connected land components (landmasses) of a map.
 *
 * Added 2026-07-05 with the island-purge softening: small islands now
 * survive to the final map, so start-position selection needs a cheap
 * "how big is the landmass under this tile" query to keep capitals off
 * sub-settleable islets.
 */

#include <cstdint>
#include "aoc/map/HexCoord.hpp"

#include <vector>

namespace aoc::map {

class HexGrid;

/// Landmass labelling of every tile, from one BFS over the grid.
struct LandmassMap {
    /// Per tile (index `row * width + col`): landmass id, or -1 for water.
    std::vector<int32_t> componentId;
    /// Tile count per landmass id.
    std::vector<int32_t> componentSize;
};

[[nodiscard]] LandmassMap computeLandmasses(const HexGrid& grid);

/// Size (tile count) of the connected land component containing each
/// tile; 0 for water tiles. Compute once per map and index by
/// `row * width + col`.
[[nodiscard]] std::vector<int32_t> computeLandmassSizes(const HexGrid& grid);

/// Resource geography around a set of start positions: the measurement
/// behind the scarcity design of the money and trade programme (plan Part
/// B3). Does anyone lack anything, and does everyone hold something a
/// neighbour lacks? "Within reach" is a hex radius around the start, wrap
/// aware on cylindrical maps.
struct ResourceGeography {
    int32_t luxuryTypes = 0;          ///< Distinct luxury goods the goods table defines
    float luxuryTypesAbsent = 0.0f;   ///< A: mean over starts of the share of luxury types not in reach
    bool everyLuxuryOnMap = false;    ///< B: every luxury type occurs somewhere on the map
    int32_t minLuxuryTypes = 0;       ///< C: fewest distinct luxury types in reach of any start
    bool copperOrIronEveryStart = false; ///< D1: copper or iron in reach of every start
    float horsesShare = 0.0f;         ///< D2: share of starts with horses in reach
    float complementaryPairs = 0.0f;  ///< E: share of start pairs where each holds a luxury the other lacks
};

inline constexpr int32_t RESOURCE_REACH_RADIUS = 9;

/// With no starts every field is zero or false; with one start E is 0.
[[nodiscard]] ResourceGeography measureResourceGeography(
    const HexGrid& grid, const std::vector<hex::AxialCoord>& starts,
    int32_t radius = RESOURCE_REACH_RADIUS);

} // namespace aoc::map
