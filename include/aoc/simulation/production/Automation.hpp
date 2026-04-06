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

#include "aoc/core/Types.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

#include <cstdint>

namespace aoc::ecs { class World; }

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
 * Reads Robot Workers from city stockpile, assigns them to automation,
 * handles maintenance consumption (1 robot per 10 turns).
 *
 * @param world       ECS world.
 * @param cityEntity  City to update.
 */
void updateCityAutomation(aoc::ecs::World& world, EntityId cityEntity);

/**
 * @brief Get the total worker capacity for a city (population + robots).
 *
 * @param population  City population.
 * @param robotWorkers Number of robot workers assigned.
 * @return Maximum recipes per turn.
 */
[[nodiscard]] constexpr int32_t totalWorkerCapacity(int32_t population,
                                                     int32_t robotWorkers) {
    // Population provides population/2 slots (min 1), robots add 1 each
    int32_t humanSlots = (population > 0) ? (population / 2) : 0;
    humanSlots = (humanSlots < 1 && population > 0) ? 1 : humanSlots;
    return humanSlots + robotWorkers;
}

} // namespace aoc::sim
