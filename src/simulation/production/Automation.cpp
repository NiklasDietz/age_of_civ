/**
 * @file Automation.cpp
 * @brief Robot worker assignment and maintenance.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/production/Automation.hpp"

#include "aoc/balance/BalanceParams.hpp"
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
        if (stockpile.consumeGoods(ROBOT_WORKERS_GOOD, 1)) {
            automation.turnsSinceLastMaintenance = 0;
            --automation.robotWorkers;
        } else {
            LOG_WARN("%s: consumeGoods failed for good %u despite prior "
                     "availability check", city.name().c_str(),
                     static_cast<unsigned>(ROBOT_WORKERS_GOOD));
        }
    }
}


int32_t totalWorkerCapacity(int32_t population, int32_t robotWorkers) {
    // Population provides population * workerCapacityPerPop slots (min 1),
    // robots add 1 each. The rate is a balance parameter rather than a hard
    // /2 so the tuner can search what is, by measurement, the binding
    // constraint on the entire production chain.
    const float rate = aoc::balance::params().workerCapacityPerPop;
    int32_t humanSlots =
        (population > 0) ? static_cast<int32_t>(static_cast<float>(population) * rate) : 0;
    humanSlots = (humanSlots < 1 && population > 0) ? 1 : humanSlots;
    return humanSlots + robotWorkers;
}

} // namespace aoc::sim
