#pragma once

/**
 * @file AttackRequest.hpp
 * @brief The one validated attack a player can order: the in-game right-click,
 *        the debug route and the MCP tool all call `requestAttack`. Reach,
 *        movement, sorties and the melee / ranged / bombing choice are decided
 *        here, so every entry point rejects the same orders for the same
 *        reasons. Until 2026-09-05 the human had no way to attack at all and
 *        the debug route attacked at any distance.
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/ErrorCodes.hpp"
#include "aoc/map/HexCoord.hpp"

namespace aoc {
class Random;
}
namespace aoc::game {
class GameState;
class Unit;
} // namespace aoc::game
namespace aoc::map {
class HexGrid;
}

namespace aoc::sim {

class DiplomacyManager;

/// The first unit of any other seat (major civs and city-states) standing on
/// `at`, or nullptr. Barbarians are not Player objects and are not found.
[[nodiscard]] aoc::game::Unit* enemyUnitAt(aoc::game::GameState& gameState, PlayerId viewer,
                                           hex::AxialCoord at);

/// Order the unit `player` owns at `from` to attack `to`.
///   - Aircraft fly a bombing run on the tile: it needs a sortie and `to` within
///     `operationalRange` (InvalidUnitAction otherwise); patrolling enemy
///     fighters in range may intercept. No enemy unit is required on the tile.
///   - Everything else needs an enemy unit on `to` (InvalidArgument) and
///     movement left; ranged units reach `range`, melee units one tile
///     (InvalidUnitAction). The attack spends the unit's movement.
/// InvalidArgument for an unknown seat, no unit at `from` or an invalid `to`;
/// InvalidUnitAction for a non-military attacker.
///   - With `diplomacy` given, attacking a major civ the player is at peace
///     with returns InvalidState (the caller declares war first, Civ VI style).
///   - A civilian defender (Settler, Builder, Trader, ...) adjacent to a melee
///     attacker is captured: it changes owner and the attacker steps onto it.
[[nodiscard]] ErrorCode requestAttack(aoc::game::GameState& gameState, aoc::Random& rng,
                                      aoc::map::HexGrid& grid, PlayerId player,
                                      hex::AxialCoord from, hex::AxialCoord to,
                                      const DiplomacyManager* diplomacy = nullptr);

} // namespace aoc::sim
