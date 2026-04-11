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

namespace aoc::sim {

/**
 * @brief Process all active spy missions. Called each turn.
 *
 * For each SpyComponent entity:
 * - Decrements turnsRemaining.
 * - On completion: rolls for success based on mission type and experience.
 * - On success: applies the mission effect (reveal tiles, steal tech, etc.).
 * - On failure: 30% chance the spy is captured (destroyed).
 * - Resets turnsRemaining for the next cycle.
 *
 * @param world  ECS world with SpyComponent entities.
 * @param rng    Random number generator for success rolls.
 */
void processSpyMissions(aoc::game::GameState& gameState, aoc::Random& rng);

/**
 * @brief Assign a spy to a mission. Validates placement.
 *
 * Sets the spy's current mission and initializes turnsRemaining
 * based on the mission duration.
 *
 * @param world   ECS world.
 * @param spy     The spy entity.
 * @param mission The mission to assign.
 * @return ErrorCode::Ok on success, or an error code on failure.
 */
[[nodiscard]] ErrorCode assignSpyMission(aoc::game::GameState& gameState,
                                         EntityId spy,
                                         SpyMission mission);

} // namespace aoc::sim
