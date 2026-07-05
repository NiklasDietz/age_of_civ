#pragma once

/**
 * @file LandmassMetrics.hpp
 * @brief Per-tile landmass (connected land component) sizes.
 *
 * Added 2026-07-05 with the island-purge softening: small islands now
 * survive to the final map, so start-position selection needs a cheap
 * "how big is the landmass under this tile" query to keep capitals off
 * sub-settleable islets.
 */

#include <cstdint>
#include <vector>

namespace aoc::map {

class HexGrid;

/// Size (tile count) of the connected land component containing each
/// tile; 0 for water tiles. One BFS over the grid — compute once per
/// map and index by `row * width + col`.
[[nodiscard]] std::vector<int32_t> computeLandmassSizes(const HexGrid& grid);

} // namespace aoc::map
