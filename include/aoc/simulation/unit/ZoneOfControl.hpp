#pragma once

/**
 * @file ZoneOfControl.hpp
 * @brief Zone of Control (ZoC) mechanics for tactical combat depth.
 *
 * Military units and encampment districts exert ZoC on adjacent tiles.
 * When a unit enters a tile in an enemy's ZoC, it must stop — all remaining
 * movement is consumed. This creates tactical depth:
 *   - Blocking mountain passes and river crossings
 *   - Protecting cities by garrisoning chokepoints
 *   - Preventing enemies from slipping past frontlines
 *
 * ZoC does NOT prevent entry — it just ends movement. A unit can still
 * attack from a ZoC tile on its next turn.
 *
 * Exceptions:
 *   - Civilian units (settlers, builders, traders) ignore ZoC
 *   - Units with Open Borders agreement ignore that player's ZoC
 *   - Embarked units ignore ZoC (already penalized by embark rules)
 */

#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"

namespace aoc::game {
class GameState;
class Unit;
}

namespace aoc::sim {

class DiplomacyManager;

/**
 * @brief Check if a tile is in an enemy's Zone of Control for the given player.
 *
 * Returns true if any enemy military unit is adjacent to the target tile.
 * Open borders agreements are respected (those units don't exert ZoC).
 *
 * @param gameState     Game state to search for enemy units.
 * @param targetTile    The tile being entered.
 * @param movingPlayer  The player whose unit is moving.
 * @param diplomacy     Diplomacy manager for open borders check.
 * @return true if the tile is in an enemy ZoC.
 */
[[nodiscard]] bool isInEnemyZoC(const aoc::game::GameState& gameState,
                                 aoc::hex::AxialCoord targetTile,
                                 PlayerId movingPlayer,
                                 const DiplomacyManager& diplomacy);

/**
 * @brief Check if a unit should have its movement consumed after entering a tile.
 *
 * Civilians, embarked units, and units with open borders bypass ZoC.
 *
 * @param unit          The unit that just moved.
 * @param targetTile    The tile it moved to.
 * @param gameState     Game state.
 * @param diplomacy     Diplomacy manager.
 * @return true if the unit's movement should be set to 0.
 */
[[nodiscard]] bool shouldConsumeMovementByZoC(const aoc::game::Unit& unit,
                                               aoc::hex::AxialCoord targetTile,
                                               const aoc::game::GameState& gameState,
                                               const DiplomacyManager& diplomacy);

} // namespace aoc::sim
