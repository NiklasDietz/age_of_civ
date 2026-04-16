#pragma once

/**
 * @file Chokepoint.hpp
 * @brief Strategic chokepoint detection at map generation time.
 *
 * Detection rules:
 *   - Land chokepoint: walkable tile with 4+ of 6 hex neighbors impassable.
 *   - Mountain pass: walkable tile surrounded by 3+ mountain tiles.
 *   - Water strait: shallow water tile where 4+ neighbors are land/mountain.
 *   - Isthmus: 1-2 tile wide land bridge between water bodies (flood fill).
 *
 * Chokepoints get +50% toll income and AI prioritizes them for garrisons and
 * city placement.
 */

namespace aoc::map { class HexGrid; }

namespace aoc::sim {

/**
 * @brief Detect and mark all chokepoints on the map.
 *
 * Runs once after terrain generation (before city placement).
 * Sets HexGrid::chokepoint() for each qualifying tile.
 *
 * @param grid  Hex grid with terrain already generated.
 */
void detectChokepoints(aoc::map::HexGrid& grid);

} // namespace aoc::sim
