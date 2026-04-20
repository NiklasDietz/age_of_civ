#pragma once

/**
 * @file EspionageSystem.hpp
 * @brief Spy mission processing and assignment.
 */

#include "aoc/core/ErrorCodes.hpp"
#include "aoc/core/Random.hpp"
#include "aoc/core/Types.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/simulation/diplomacy/Espionage.hpp"

#include <cstdint>

namespace aoc::game { class Unit; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

class DiplomacyManager;

/**
 * @brief Process all active spy missions. Called each turn.
 *
 * Iterates all units in the GameState object model. Units with an active spy
 * mission (turnsRemaining > 0) are processed for success or failure.
 *
 * Failed missions with Identified / Captured / Killed outcomes generate
 * political fallout against the spy's owner: grievance on the victim and a
 * relation penalty. Pass a non-null @p diplomacy to apply the relation hit.
 *
 * @param gameState  Top-level game state.
 * @param rng        Random number generator for success rolls.
 * @param diplomacy  Optional diplomacy manager for relation penalties.
 */
void processSpyMissions(aoc::game::GameState& gameState,
                        const aoc::map::HexGrid& grid,
                        aoc::Random& rng,
                        DiplomacyManager* diplomacy = nullptr);

/**
 * @brief Assign a spy mission to a unit that has spy capability.
 *
 * Sets the unit's SpyComponent mission and initialises turnsRemaining.
 * The caller is responsible for ensuring the unit is a valid spy unit.
 *
 * @param gameState  Top-level game state (unused but kept for API symmetry).
 * @param spyUnit    The unit to assign the mission to.
 * @param mission    The mission type to assign.
 * @return ErrorCode::Ok on success.
 */
[[nodiscard]] ErrorCode assignSpyMission(aoc::game::GameState& gameState,
                                         aoc::game::Unit& spyUnit,
                                         SpyMission mission);

} // namespace aoc::sim
