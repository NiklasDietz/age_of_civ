/**
 * @file Automation.cpp
 * @brief Robot worker assignment and maintenance.
 */

#include "aoc/simulation/production/Automation.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

void updateCityAutomation(aoc::ecs::World& world, EntityId cityEntity) {
    CityStockpileComponent* stockpile =
        world.tryGetComponent<CityStockpileComponent>(cityEntity);
    if (stockpile == nullptr) {
        return;
    }

    int32_t robotsAvailable = stockpile->getAmount(ROBOT_WORKERS_GOOD);
    if (robotsAvailable <= 0) {
        // Remove automation component if no robots
        CityAutomationComponent* automation =
            world.tryGetComponent<CityAutomationComponent>(cityEntity);
        if (automation != nullptr) {
            automation->robotWorkers = 0;
        }
        return;
    }

    // Get or create automation component
    CityAutomationComponent* automation =
        world.tryGetComponent<CityAutomationComponent>(cityEntity);
    if (automation == nullptr) {
        CityAutomationComponent newAuto{};
        world.addComponent<CityAutomationComponent>(cityEntity, std::move(newAuto));
        automation = world.tryGetComponent<CityAutomationComponent>(cityEntity);
    }
    if (automation == nullptr) {
        return;
    }

    // Assign all available robots
    automation->robotWorkers = robotsAvailable;

    // Maintenance: consume 1 robot per ROBOT_MAINTENANCE_INTERVAL turns
    ++automation->turnsSinceLastMaintenance;
    if (automation->turnsSinceLastMaintenance >= ROBOT_MAINTENANCE_INTERVAL
        && robotsAvailable > 0) {
        stockpile->consumeGoods(ROBOT_WORKERS_GOOD, 1);
        automation->turnsSinceLastMaintenance = 0;
        --automation->robotWorkers;
    }
}

} // namespace aoc::sim
