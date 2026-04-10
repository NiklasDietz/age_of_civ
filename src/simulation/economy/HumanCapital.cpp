/**
 * @file HumanCapital.cpp
 * @brief Education, literacy, and human capital development.
 */

#include "aoc/simulation/economy/HumanCapital.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/ecs/World.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim {

void updateHumanCapital(aoc::ecs::World& world, PlayerId player) {
    aoc::ecs::ComponentPool<PlayerHumanCapitalComponent>* hcPool =
        world.getPool<PlayerHumanCapitalComponent>();
    if (hcPool == nullptr) {
        return;
    }

    PlayerHumanCapitalComponent* hc = nullptr;
    for (uint32_t i = 0; i < hcPool->size(); ++i) {
        if (hcPool->data()[i].owner == player) {
            hc = &hcPool->data()[i];
            break;
        }
    }
    if (hc == nullptr) {
        return;
    }

    // Count education buildings and total population
    aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    if (cityPool == nullptr) {
        return;
    }

    int32_t totalPopulation = 0;
    int32_t educationCapacity = 0;  // How many citizens can be educated

    for (uint32_t i = 0; i < cityPool->size(); ++i) {
        if (cityPool->data()[i].owner != player) { continue; }
        totalPopulation += cityPool->data()[i].population;

        EntityId cityEntity = cityPool->entities()[i];
        const CityDistrictsComponent* districts =
            world.tryGetComponent<CityDistrictsComponent>(cityEntity);
        if (districts == nullptr) { continue; }

        for (const CityDistrictsComponent::PlacedDistrict& d : districts->districts) {
            if (d.type == DistrictType::Campus) {
                // Campus district itself: educates 2 citizens
                educationCapacity += 2;
                for (BuildingId bid : d.buildings) {
                    switch (bid.value) {
                        case 7:  educationCapacity += 3;  break;  // Library: +3
                        case 19: educationCapacity += 5;  break;  // University: +5
                        case 12: educationCapacity += 8;  break;  // Research Lab: +8
                        default: break;
                    }
                }
            }
            // Monument provides basic literacy (oral tradition)
            for (BuildingId bid : d.buildings) {
                if (bid.value == 16) { educationCapacity += 1; }  // Monument: +1
            }
        }
    }

    if (totalPopulation <= 0) {
        return;
    }

    // Target literacy: fraction of population that can be educated
    float targetLiteracy = static_cast<float>(educationCapacity)
                         / static_cast<float>(totalPopulation);
    targetLiteracy = std::clamp(targetLiteracy, 0.02f, 1.0f);

    // Literacy adjusts 5% toward target per turn (education is slow)
    float diff = targetLiteracy - hc->literacyRate;
    if (diff > 0.0f) {
        // Growing: 5% per turn toward target
        hc->literacyRate += diff * 0.05f;
    } else {
        // Decay without education infrastructure: 2% per turn toward target
        hc->literacyRate += diff * 0.02f;
    }

    hc->literacyRate = std::clamp(hc->literacyRate, 0.02f, 1.0f);
}

} // namespace aoc::sim
