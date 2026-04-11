/**
 * @file TechUnemployment.cpp
 * @brief Technological unemployment from automation.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/simulation/economy/TechUnemployment.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/economy/IndustrialRevolution.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim {

void updateUnemployment(CityUnemploymentComponent& unemployment,
                         int32_t automationBuildings,
                         int32_t population,
                         float educationLevel,
                         int32_t industrialRevLevel) {
    if (population <= 0) {
        unemployment.unemploymentRate = 0.0f;
        return;
    }

    // Jobs displaced by automation: each automation building displaces 1-2 workers
    // Industrial revolution compounds this effect
    float displacementPerBuilding = 1.0f + static_cast<float>(industrialRevLevel) * 0.3f;
    float totalDisplaced = static_cast<float>(automationBuildings) * displacementPerBuilding;
    unemployment.automationDisplacement = static_cast<int32_t>(totalDisplaced);

    // Target unemployment rate from automation
    float automationUnemployment = totalDisplaced / static_cast<float>(population);

    // Education mitigates: educated workers retrain faster
    // Each 0.1 education reduces unemployment by 10%
    float educationMitigation = educationLevel * 0.5f;  // Up to 50% reduction
    automationUnemployment *= (1.0f - educationMitigation);

    // Natural unemployment floor (2%) + structural rate
    float targetRate = 0.02f + automationUnemployment;
    targetRate = std::clamp(targetRate, 0.0f, 0.50f);  // Cap at 50%

    // Unemployment adjusts 15% toward target per turn (slow response)
    unemployment.unemploymentRate = unemployment.unemploymentRate * 0.85f
                                  + targetRate * 0.15f;
    unemployment.unemploymentRate = std::clamp(unemployment.unemploymentRate, 0.0f, 0.50f);
}

void processUnemployment(aoc::game::GameState& gameState, PlayerId player) {
    aoc::ecs::World& world = gameState.legacyWorld();
    aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    if (cityPool == nullptr) {
        return;
    }

    // Get industrial revolution level
    int32_t industrialLevel = 0;
    const aoc::ecs::ComponentPool<PlayerIndustrialComponent>* indPool =
        world.getPool<PlayerIndustrialComponent>();
    if (indPool != nullptr) {
        for (uint32_t i = 0; i < indPool->size(); ++i) {
            if (indPool->data()[i].owner == player) {
                industrialLevel = static_cast<int32_t>(indPool->data()[i].currentRevolution);
                break;
            }
        }
    }

    // Get education level (from Campus buildings as proxy)
    float educationLevel = 0.0f;
    int32_t campusBuildings = 0;
    int32_t totalCities = 0;

    for (uint32_t i = 0; i < cityPool->size(); ++i) {
        if (cityPool->data()[i].owner != player) { continue; }
        ++totalCities;
        EntityId cityEntity = cityPool->entities()[i];

        const CityDistrictsComponent* districts =
            world.tryGetComponent<CityDistrictsComponent>(cityEntity);
        if (districts == nullptr) { continue; }

        for (const CityDistrictsComponent::PlacedDistrict& d : districts->districts) {
            if (d.type == DistrictType::Campus) {
                campusBuildings += static_cast<int32_t>(d.buildings.size());
            }
        }
    }
    if (totalCities > 0) {
        // Education: 0.1 per campus building per city, cap at 0.8
        educationLevel = static_cast<float>(campusBuildings)
                       / static_cast<float>(totalCities) * 0.20f;
        educationLevel = std::min(educationLevel, 0.80f);
    }

    // Process each city
    for (uint32_t i = 0; i < cityPool->size(); ++i) {
        if (cityPool->data()[i].owner != player) { continue; }
        EntityId cityEntity = cityPool->entities()[i];

        CityUnemploymentComponent* unemployment =
            world.tryGetComponent<CityUnemploymentComponent>(cityEntity);
        if (unemployment == nullptr) { continue; }

        // Count automation buildings in this city
        int32_t automationCount = 0;
        const CityDistrictsComponent* districts =
            world.tryGetComponent<CityDistrictsComponent>(cityEntity);
        if (districts != nullptr) {
            for (const CityDistrictsComponent::PlacedDistrict& d : districts->districts) {
                if (d.type == DistrictType::Industrial) {
                    // Factory(3), Electronics(4), Semiconductor(11), Industrial Complex(5)
                    for (BuildingId bid : d.buildings) {
                        if (bid.value == 3 || bid.value == 4
                            || bid.value == 5 || bid.value == 11) {
                            ++automationCount;
                        }
                    }
                }
            }
        }

        updateUnemployment(*unemployment, automationCount,
                          cityPool->data()[i].population,
                          educationLevel, industrialLevel);
    }
}

} // namespace aoc::sim
