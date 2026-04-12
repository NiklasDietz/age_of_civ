/**
 * @file Automation.cpp
 * @brief Robot worker assignment and maintenance.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/production/Automation.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

void updateCityAutomation(aoc::game::City& city) {
    CityStockpileComponent& stockpile = city.stockpile();

    int32_t robotsAvailable = stockpile.getAmount(ROBOT_WORKERS_GOOD);
    if (robotsAvailable <= 0) {
        city.automation().robotWorkers = 0;
        return;
    }

    CityAutomationComponent& automation = city.automation();
    automation.robotWorkers = robotsAvailable;

    // Maintenance: consume 1 robot per ROBOT_MAINTENANCE_INTERVAL turns
    ++automation.turnsSinceLastMaintenance;
    if (automation.turnsSinceLastMaintenance >= ROBOT_MAINTENANCE_INTERVAL
        && robotsAvailable > 0) {
        [[maybe_unused]] bool ok = stockpile.consumeGoods(ROBOT_WORKERS_GOOD, 1);
        automation.turnsSinceLastMaintenance = 0;
        --automation.robotWorkers;
    }
}

} // namespace aoc::sim
