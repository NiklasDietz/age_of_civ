#pragma once

/**
 * @file NavalPassage.hpp
 * @brief Naval passage violation detection and escalation.
 *
 * Mirrors the land border violation system (BorderViolation.hpp) for water
 * tiles. Naval military units in owned waters without Open Borders trigger
 * the same escalation cascade: warning, reputation penalty, casus belli.
 * Near-harbor proximity (2 tiles) doubles escalation speed.
 */

#include "aoc/core/Types.hpp"

namespace aoc::game { class GameState; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

class DiplomacyManager;

/**
 * @brief Scan all naval military units and update naval passage violation state.
 *
 * Called once per turn during the global phase, after land border violations.
 * For each player pair (violator, waters owner), counts naval military units
 * present without Open Borders and updates the escalation cascade stored in
 * PairwiseRelation::navalUnitsInWaters and turnsWithNavalViolation.
 *
 * @param gameState  Full game state with players and units.
 * @param grid       Hex grid for tile ownership and terrain lookup.
 * @param diplomacy  Diplomacy manager for relation updates and reputation modifiers.
 */
void updateNavalPassageViolations(aoc::game::GameState& gameState,
                                   const aoc::map::HexGrid& grid,
                                   DiplomacyManager& diplomacy);

} // namespace aoc::sim
