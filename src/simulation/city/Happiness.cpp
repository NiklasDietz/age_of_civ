/**
 * @file Happiness.cpp
 * @brief City happiness calculation.
 */

#include "aoc/simulation/city/Happiness.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/monetary/Inflation.hpp"
#include "aoc/simulation/monetary/FiscalPolicy.hpp"
#include "aoc/ecs/World.hpp"

namespace aoc::sim {

void computeCityHappiness(aoc::ecs::World& world, PlayerId player) {
    aoc::ecs::ComponentPool<CityComponent>* cityPool = world.getPool<CityComponent>();
    if (cityPool == nullptr) {
        return;
    }

    // Find player's monetary state for inflation/tax penalties
    float inflationPenalty = 0.0f;
    float taxPenalty = 0.0f;
    const aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();
    if (monetaryPool != nullptr) {
        for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
            const MonetaryStateComponent& ms = monetaryPool->data()[i];
            if (ms.owner == player) {
                inflationPenalty = inflationHappinessPenalty(ms.inflationRate);
                taxPenalty = -taxHappinessModifier(ms.taxRate);  // Negative modifier becomes penalty
                break;
            }
        }
    }

    for (uint32_t i = 0; i < cityPool->size(); ++i) {
        CityComponent& city = cityPool->data()[i];
        if (city.owner != player) {
            continue;
        }

        EntityId cityEntity = cityPool->entities()[i];

        // Get or create happiness component
        if (!world.hasComponent<CityHappinessComponent>(cityEntity)) {
            world.addComponent<CityHappinessComponent>(cityEntity, CityHappinessComponent{});
        }
        CityHappinessComponent& happiness = world.getComponent<CityHappinessComponent>(cityEntity);

        // Base amenities: 1 from palace/capital + future: luxury resources, buildings
        happiness.amenities = 1.0f;

        // Demand: 1 per 2 citizens
        happiness.demand = static_cast<float>(city.population) * 0.5f;

        // Modifiers from economy
        happiness.modifiers = -inflationPenalty - taxPenalty;

        // Net happiness
        happiness.happiness = happiness.amenities - happiness.demand + happiness.modifiers;
    }
}

} // namespace aoc::sim
