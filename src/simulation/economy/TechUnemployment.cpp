/**
 * @file TechUnemployment.cpp
 * @brief Technological unemployment from automation.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/economy/TechUnemployment.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/economy/IndustrialRevolution.hpp"

#include <algorithm>

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

    float displacementPerBuilding = 1.0f + static_cast<float>(industrialRevLevel) * 0.3f;
    float totalDisplaced = static_cast<float>(automationBuildings) * displacementPerBuilding;
    unemployment.automationDisplacement = static_cast<int32_t>(totalDisplaced);

    float automationUnemployment = totalDisplaced / static_cast<float>(population);

    float educationMitigation = educationLevel * 0.5f;
    automationUnemployment *= (1.0f - educationMitigation);

    float targetRate = 0.02f + automationUnemployment;
    targetRate = std::clamp(targetRate, 0.0f, 0.50f);

    unemployment.unemploymentRate = unemployment.unemploymentRate * 0.85f
                                  + targetRate * 0.15f;
    unemployment.unemploymentRate = std::clamp(unemployment.unemploymentRate, 0.0f, 0.50f);
}

void processUnemployment(aoc::game::GameState& gameState, PlayerId player) {
    aoc::game::Player* playerObj = gameState.player(player);
    if (playerObj == nullptr) {
        return;
    }

    const int32_t industrialLevel =
        static_cast<int32_t>(playerObj->industrial().currentRevolution);

    // Compute education level from campus buildings across all cities
    float   educationLevel  = 0.0f;
    int32_t campusBuildings = 0;
    int32_t totalCities     = 0;

    for (const std::unique_ptr<aoc::game::City>& cityPtr : playerObj->cities()) {
        if (cityPtr == nullptr) { continue; }
        ++totalCities;
        for (const CityDistrictsComponent::PlacedDistrict& d : cityPtr->districts().districts) {
            if (d.type == DistrictType::Campus) {
                campusBuildings += static_cast<int32_t>(d.buildings.size());
            }
        }
    }
    if (totalCities > 0) {
        educationLevel = static_cast<float>(campusBuildings)
                       / static_cast<float>(totalCities) * 0.20f;
        educationLevel = std::min(educationLevel, 0.80f);
    }

    for (const std::unique_ptr<aoc::game::City>& cityPtr : playerObj->cities()) {
        if (cityPtr == nullptr) { continue; }

        CityUnemploymentComponent& unemployment = cityPtr->unemployment();

        int32_t automationCount = 0;
        for (const CityDistrictsComponent::PlacedDistrict& d : cityPtr->districts().districts) {
            if (d.type == DistrictType::Industrial) {
                for (BuildingId bid : d.buildings) {
                    if (bid.value == 3 || bid.value == 4
                        || bid.value == 5 || bid.value == 11) {
                        ++automationCount;
                    }
                }
            }
        }

        updateUnemployment(unemployment, automationCount,
                           cityPtr->population(),
                           educationLevel, industrialLevel);
    }
}

} // namespace aoc::sim
