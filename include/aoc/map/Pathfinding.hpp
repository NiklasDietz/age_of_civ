#pragma once

/**
 * @file Pathfinding.hpp
 * @brief A* pathfinding on the hex grid.
 */

#include "aoc/map/HexCoord.hpp"
#include "aoc/core/Types.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace aoc::game {
class GameState;
}

namespace aoc::map {

class HexGrid;

struct PathResult {
    std::vector<hex::AxialCoord> path;  ///< Sequence of tiles from start to goal (inclusive).
    int32_t totalCost = 0;              ///< Sum of movement costs along the path.
};

/**
 * @brief Find the shortest path between two hex tiles using A*.
 *
 * @param grid          The hex grid with movement cost data.
 * @param start         Starting tile (axial coordinates).
 * @param goal          Destination tile (axial coordinates).
 * @param maxCost       Maximum total movement cost (0 = unlimited).
 * @param world         Optional ECS world for ZoC-aware costing. If non-null,
 *                      tiles in an enemy zone of control cost +3 extra.
 * @param movingPlayer  The player whose units are pathfinding (needed for ZoC check).
 * @return PathResult if a path exists, std::nullopt if unreachable.
 */
[[nodiscard]] std::optional<PathResult> findPath(
    const HexGrid& grid,
    hex::AxialCoord start,
    hex::AxialCoord goal,
    int32_t maxCost = 0,
    const aoc::game::GameState* gameState = nullptr,
    PlayerId movingPlayer = INVALID_PLAYER,
    bool isNavalPath = false);

/**
 * @brief Get all tiles reachable from a starting tile within a movement budget.
 *
 * Useful for showing movement range highlights.
 *
 * @param grid         The hex grid with movement cost data.
 * @param start        Starting tile (axial coordinates).
 * @param maxCost      Maximum movement points.
 * @return Vector of (tile, remaining cost) pairs for all reachable tiles.
 */
struct ReachableTile {
    hex::AxialCoord coord;
    int32_t remainingCost;
};

[[nodiscard]] std::vector<ReachableTile> findReachable(
    const HexGrid& grid,
    hex::AxialCoord start,
    int32_t maxCost);

} // namespace aoc::map
