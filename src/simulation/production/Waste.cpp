/**
 * @file Waste.cpp
 * @brief Production waste accumulation and treatment.
 */

#include "aoc/simulation/production/Waste.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"

#include <algorithm>

namespace aoc::sim {

void processWasteTreatment(aoc::game::City& city) {
    CityPollutionComponent& pollution = city.pollution();
    if (pollution.wasteAccumulated <= 0) {
        return;
    }

    // Only treat waste if the city has a Waste Treatment Plant
    if (!city.districts().hasBuilding(WASTE_TREATMENT_PLANT)) {
        return;
    }

    // Process up to 5 waste per turn, converting to Construction Materials
    constexpr int32_t TREATMENT_RATE = 5;
    int32_t treated = std::min(pollution.wasteAccumulated, TREATMENT_RATE);
    pollution.wasteAccumulated -= treated;

    // Convert to construction materials (recycling)
    if (treated > 0) {
        city.stockpile().addGoods(goods::CONSTRUCTION_MAT, treated / 2);
    }
}

void accumulateWaste(aoc::game::City& city, BuildingId buildingUsed, int32_t batchesExecuted) {
    const WasteOutput waste = buildingWasteOutput(buildingUsed);
    if (waste.amount <= 0
        || waste.type == static_cast<WasteType>(static_cast<uint8_t>(WasteType::Count))) {
        return;
    }

    const int32_t totalWaste = waste.amount * batchesExecuted;
    if (totalWaste <= 0) {
        return;
    }

    CityPollutionComponent& pollution = city.pollution();
    pollution.wasteAccumulated += totalWaste;

    // Emissions contribute to CO2
    if (waste.type == WasteType::Emissions) {
        pollution.co2ContributionPerTurn += totalWaste;
    }
}

int32_t totalIndustrialCO2(const aoc::game::GameState& gameState) {
    int32_t total = 0;
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) {
            continue;
        }
        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            if (cityPtr == nullptr) {
                continue;
            }
            total += cityPtr->pollution().co2ContributionPerTurn;
        }
    }
    return total;
}

} // namespace aoc::sim
