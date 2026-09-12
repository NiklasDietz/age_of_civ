#pragma once

/**
 * @file CityConnection.hpp
 * @brief City road connection detection and gold bonuses.
 *
 * Cities connected to the capital via roads receive a gold bonus each turn.
 */

#include "aoc/map/HexCoord.hpp"
#include "aoc/core/Types.hpp"

namespace aoc::map {
class HexGrid;
}

namespace aoc::game {
class Player;
}

namespace aoc::sim {

/**
 * @brief Check if two positions are connected via road tiles using BFS.
 *
 * Only steps through tiles where the grid has a road (hasRoad() == true).
 * Both the start and end positions must themselves have roads (or be city tiles).
 *
 * @param grid       The hex grid.
 * @param cityPos    Source city position.
 * @param capitalPos Capital city position.
 * @return true if a road path exists between the two positions.
 */
[[nodiscard]] bool isCityConnected(const aoc::map::HexGrid& grid,
                                    hex::AxialCoord cityPos,
                                    hex::AxialCoord capitalPos);

/// How many of the player's other cities a road joins to the capital. A
/// connection used to pay gold from nowhere; it is now a point of collection
/// efficiency (Maintenance.hpp), so this only counts.
[[nodiscard]] int32_t connectedCityCount(const aoc::game::Player& player,
                                         const aoc::map::HexGrid& grid);

} // namespace aoc::sim
