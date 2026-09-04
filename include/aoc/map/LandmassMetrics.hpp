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

} // namespace aoc::map
