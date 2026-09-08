#pragma once

/**
 * @file Automation.hpp
 * @brief Late-game automation via robot workers.
 *
 * Robot Workers are a produced good (Microchips + Steel + Electronics).
 * A city with Robot Workers in stockpile can execute additional recipes
 * beyond the population-based worker limit.
 *
 * Each Robot Worker provides 1 additional recipe slot per turn.
 * Robot workers are consumed slowly (1 per 10 turns = maintenance/repair).
 *
 * Robot workers:
 *   + Pure production capacity (no food, housing, or amenity cost)
 *   - Don't generate culture, science, or faith
 *   - Don't count as population for voting, loyalty, or border pressure
 *   - Require energy (5 per robot worker, adds to power grid demand)
 *
 * This creates a late-game "automated economy" path: fewer citizens
 * needed for production, freeing population for science/culture.
 */

#include <cstdint>

namespace aoc::game { class City; }

namespace aoc::sim {

/// Good ID for Robot Workers.
inline constexpr uint16_t ROBOT_WORKERS_GOOD = 143;

/// Energy demand per robot worker per turn.
constexpr int32_t ROBOT_ENERGY_DEMAND = 5;

/// Robot maintenance: 1 robot consumed per this many turns.
constexpr int32_t ROBOT_MAINTENANCE_INTERVAL = 10;

// ============================================================================
// Automation state (ECS component)
// ============================================================================

struct CityAutomationComponent {
    int32_t robotWorkers = 0;            ///< Current robot workers assigned
    int32_t turnsSinceLastMaintenance = 0; ///< Counter for maintenance consumption

    /// Additional recipe slots provided by robots.
    [[nodiscard]] int32_t bonusRecipeSlots() const {
        return this->robotWorkers;
    }

    /// Energy demand from robots (added to city power demand).
    [[nodiscard]] int32_t robotEnergyDemand() const {
        return this->robotWorkers * ROBOT_ENERGY_DEMAND;
    }
};

/**
 * @brief Update automation state for a city.
 *
 * Reads Robot Workers from the city's stockpile, assigns them to the
 * automation component, and handles maintenance consumption (1 robot per
 * ROBOT_MAINTENANCE_INTERVAL turns).
 */
void updateCityAutomation(aoc::game::City& city);

/**
 * @brief Get the total worker capacity for a city (population + robots).
 *
 * @param population  City population.
 * @param robotWorkers Number of robot workers assigned.
 * @return Maximum recipes per turn.
 */
/// No longer constexpr: the per-population rate is a BALANCE PARAMETER
/// (`workerCapacityPerPop`, default 0.50) so the tuner can search it. It is the
/// measured binding constraint on the production chain -- 98.9 % of city-turns
/// exhaust their labour budget at the shipped rate -- and it was invisible to
/// the GA while it lived here as a hard-coded division.
[[nodiscard]] int32_t totalWorkerCapacity(int32_t population, int32_t robotWorkers);

} // namespace aoc::sim
