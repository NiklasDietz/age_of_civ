#pragma once

/**
 * @file BorderViolation.hpp
 * @brief Soft border violation detection and escalation.
 *
 * Units CAN enter foreign territory without Open Borders. The consequences
 * are diplomatic, not mechanical barriers:
 *
 *   1. First unit enters -> warning notification to territory owner
 *   2. Units remain 3+ turns -> reputation penalty -3/turn
 *   3. Units remain 10+ turns -> casus belli granted
 *   4. Military units near cities (within 3 tiles) -> 2x escalation speed
 *
 * The system updates PairwiseRelation::unitsInTerritory, turnsWithViolation,
 * and casusBelliGranted fields each turn.
 */

#include "aoc/core/Types.hpp"

namespace aoc::game { class GameState; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

class DiplomacyManager;

/**
 * @brief Scan all military units and update border violation state.
 *
 * Called once per turn during the global phase (after movement, before AI decisions).
 * For each player pair (violator, territory owner), counts military units present
 * without Open Borders and updates the escalation cascade.
 *
 * @param gameState  Full game state with players and units.
 * @param grid       Hex grid for tile ownership lookup.
 * @param diplomacy  Diplomacy manager for relation updates and reputation modifiers.
 */
void updateBorderViolations(aoc::game::GameState& gameState,
                             const aoc::map::HexGrid& grid,
                             DiplomacyManager& diplomacy);

} // namespace aoc::sim
