/**
 * @file Waste.cpp
 * @brief Production waste accumulation and treatment.
 */

#include "aoc/simulation/production/Waste.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

void processWasteTreatment(aoc::ecs::World& world, EntityId cityEntity) {
    CityPollutionComponent* pollution =
        world.tryGetComponent<CityPollutionComponent>(cityEntity);
    if (pollution == nullptr || pollution->wasteAccumulated <= 0) {
        return;
    }

    // Check if city has a Waste Treatment Plant
    const CityDistrictsComponent* districts =
        world.tryGetComponent<CityDistrictsComponent>(cityEntity);
    if (districts == nullptr || !districts->hasBuilding(WASTE_TREATMENT_PLANT)) {
        return;
    }

    // Process up to 5 waste per turn, converting to Construction Materials
    constexpr int32_t TREATMENT_RATE = 5;
    int32_t treated = std::min(pollution->wasteAccumulated, TREATMENT_RATE);
    pollution->wasteAccumulated -= treated;

    // Convert to construction materials (recycling)
    CityStockpileComponent* stockpile =
        world.tryGetComponent<CityStockpileComponent>(cityEntity);
    if (stockpile != nullptr && treated > 0) {
        stockpile->addGoods(goods::CONSTRUCTION_MAT, treated / 2);
    }
}

void accumulateWaste(aoc::ecs::World& world, EntityId cityEntity,
                     BuildingId buildingUsed, int32_t batchesExecuted) {
    WasteOutput waste = buildingWasteOutput(buildingUsed);
    if (waste.amount <= 0
        || waste.type == static_cast<WasteType>(static_cast<uint8_t>(WasteType::Count))) {
        return;
    }

    int32_t totalWaste = waste.amount * batchesExecuted;
    if (totalWaste <= 0) {
        return;
    }

    // Get or create pollution component
    CityPollutionComponent* pollution =
        world.tryGetComponent<CityPollutionComponent>(cityEntity);
    if (pollution == nullptr) {
        CityPollutionComponent newPollution{};
        world.addComponent<CityPollutionComponent>(cityEntity, std::move(newPollution));
        pollution = world.tryGetComponent<CityPollutionComponent>(cityEntity);
    }
    if (pollution != nullptr) {
        pollution->wasteAccumulated += totalWaste;

        // Emissions contribute to CO2
        if (waste.type == WasteType::Emissions) {
            pollution->co2ContributionPerTurn += totalWaste;
        }
    }
}

int32_t totalIndustrialCO2(const aoc::ecs::World& world) {
    const aoc::ecs::ComponentPool<CityPollutionComponent>* pool =
        world.getPool<CityPollutionComponent>();
    if (pool == nullptr) {
        return 0;
    }

    int32_t total = 0;
    for (uint32_t i = 0; i < pool->size(); ++i) {
        total += pool->data()[i].co2ContributionPerTurn;
    }
    return total;
}

} // namespace aoc::sim
