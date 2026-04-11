/**
 * @file PowerGrid.cpp
 * @brief City power grid computation, fuel consumption, and nuclear meltdown.
 */

#include "aoc/simulation/production/PowerGrid.hpp"
#include "aoc/simulation/production/Waste.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

CityPowerComponent computeCityPower(aoc::ecs::World& world,
                                     const aoc::map::HexGrid& grid,
                                     EntityId cityEntity) {
    CityPowerComponent result{};

    const CityDistrictsComponent* districts =
        world.tryGetComponent<CityDistrictsComponent>(cityEntity);
    if (districts == nullptr) {
        return result;
    }

    CityStockpileComponent* stockpile =
        world.tryGetComponent<CityStockpileComponent>(cityEntity);

    const CityComponent* city = world.tryGetComponent<CityComponent>(cityEntity);

    // Check each power plant building
    for (const PowerPlantDef& plantDef : POWER_PLANT_DEFS) {
        if (!districts->hasBuilding(plantDef.buildingId)) {
            continue;
        }

        // Hydroelectric requires river adjacency
        if (plantDef.requiresRiver && city != nullptr) {
            bool hasRiver = false;
            int32_t cityIndex = grid.toIndex(city->location);
            if (grid.isValid(city->location) && grid.riverEdges(cityIndex) != 0) {
                hasRiver = true;
            }
            if (!hasRiver) {
                continue;  // No river, hydroelectric doesn't work
            }
        }

        // Consume fuel if required
        if (plantDef.fuelGoodId != 0xFFFF && stockpile != nullptr) {
            int32_t available = stockpile->getAmount(plantDef.fuelGoodId);
            if (available < plantDef.fuelPerTurn) {
                continue;  // Not enough fuel, plant doesn't operate
            }
            [[maybe_unused]] bool ok = stockpile->consumeGoods(plantDef.fuelGoodId, plantDef.fuelPerTurn);
        }

        result.energySupply += plantDef.energyOutput;

        // Track emissions as waste
        if (plantDef.emissions > 0) {
            CityPollutionComponent* pollution =
                world.tryGetComponent<CityPollutionComponent>(cityEntity);
            if (pollution == nullptr) {
                CityPollutionComponent newPollution{};
                world.addComponent<CityPollutionComponent>(cityEntity, std::move(newPollution));
                pollution = world.tryGetComponent<CityPollutionComponent>(cityEntity);
            }
            if (pollution != nullptr) {
                pollution->co2ContributionPerTurn += plantDef.emissions;
                pollution->wasteAccumulated += plantDef.emissions;
            }
        }

        if (plantDef.type == PowerPlantType::Nuclear) {
            result.hasNuclear = true;
        }
    }

    // Compute energy demand from industrial buildings
    for (const CityDistrictsComponent::PlacedDistrict& district : districts->districts) {
        for (BuildingId bid : district.buildings) {
            result.energyDemand += buildingEnergyDemand(bid);
        }
    }

    return result;
}

/// Apply fallout to tiles in a hex radius around a city.
static void applyFalloutRadius(aoc::map::HexGrid& grid, aoc::hex::AxialCoord center,
                                int32_t radius, int16_t durationTurns) {
    std::vector<aoc::hex::AxialCoord> affected;
    affected.reserve(static_cast<std::size_t>(radius * radius * 3 + 1));
    aoc::hex::spiral(center, radius, std::back_inserter(affected));
    affected.push_back(center);

    for (const aoc::hex::AxialCoord& tile : affected) {
        if (!grid.isValid(tile)) { continue; }
        int32_t idx = grid.toIndex(tile);
        if (aoc::map::isWater(grid.terrain(idx))) { continue; }
        grid.applyFallout(idx, durationTurns);
    }
}

bool checkNuclearMeltdown(aoc::ecs::World& world, aoc::map::HexGrid& grid,
                          EntityId cityEntity, uint32_t turnHash) {
    const CityDistrictsComponent* districts =
        world.tryGetComponent<CityDistrictsComponent>(cityEntity);
    if (districts == nullptr) {
        return false;
    }

    // Find the nuclear plant
    bool hasNuclear = false;
    for (const CityDistrictsComponent::PlacedDistrict& district : districts->districts) {
        for (BuildingId bid : district.buildings) {
            if (bid == POWER_PLANT_DEFS[3].buildingId) {
                hasNuclear = true;
                break;
            }
        }
        if (hasNuclear) { break; }
    }
    if (!hasNuclear) {
        return false;
    }

    // 0.2% chance per turn (hash-based deterministic check)
    constexpr uint32_t MELTDOWN_THRESHOLD = 20;  // 20/10000 = 0.2%
    uint32_t roll = (turnHash * 2654435761u) % 10000;
    if (roll >= MELTDOWN_THRESHOLD) {
        return false;
    }

    // MELTDOWN!
    const CityComponent* city = world.tryGetComponent<CityComponent>(cityEntity);
    const char* cityName = (city != nullptr) ? city->name.c_str() : "unknown";

    LOG_INFO("NUCLEAR MELTDOWN in city %s! Massive pollution, population halved.",
             cityName);

    // Destroy the nuclear plant building
    CityDistrictsComponent* mutableDistricts =
        world.tryGetComponent<CityDistrictsComponent>(cityEntity);
    if (mutableDistricts != nullptr) {
        for (CityDistrictsComponent::PlacedDistrict& district : mutableDistricts->districts) {
            std::vector<BuildingId>::iterator it = std::find(district.buildings.begin(), district.buildings.end(),
                                POWER_PLANT_DEFS[3].buildingId);
            if (it != district.buildings.end()) {
                district.buildings.erase(it);
                break;
            }
        }
    }

    // Massive pollution
    CityPollutionComponent* pollution =
        world.tryGetComponent<CityPollutionComponent>(cityEntity);
    if (pollution == nullptr) {
        CityPollutionComponent newPollution{};
        world.addComponent<CityPollutionComponent>(cityEntity, std::move(newPollution));
        pollution = world.tryGetComponent<CityPollutionComponent>(cityEntity);
    }
    if (pollution != nullptr) {
        pollution->wasteAccumulated += 100;
    }

    // Halve population
    CityComponent* mutableCity = world.tryGetComponent<CityComponent>(cityEntity);
    if (mutableCity != nullptr && mutableCity->population > 1) {
        mutableCity->population /= 2;
    }

    // Apply fallout to 1-hex radius around city for 20 turns
    if (city != nullptr) {
        applyFalloutRadius(grid, city->location, 1, 20);
        LOG_INFO("Nuclear fallout applied: 1-hex radius, 20 turns around %s", cityName);
    }

    return true;
}

void applyBombedNuclearFallout(aoc::ecs::World& world, aoc::map::HexGrid& grid,
                                EntityId cityEntity) {
    const CityComponent* city = world.tryGetComponent<CityComponent>(cityEntity);
    if (city == nullptr) {
        return;
    }

    // Check if the city has a nuclear plant
    const CityDistrictsComponent* districts =
        world.tryGetComponent<CityDistrictsComponent>(cityEntity);
    if (districts == nullptr || !districts->hasBuilding(POWER_PLANT_DEFS[3].buildingId)) {
        return;
    }

    LOG_INFO("NUCLEAR PLANT BOMBED in %s! Massive fallout, 2-hex radius, 40 turns.",
             city->name.c_str());

    // Destroy the nuclear plant
    CityDistrictsComponent* mutableDistricts =
        world.tryGetComponent<CityDistrictsComponent>(cityEntity);
    if (mutableDistricts != nullptr) {
        for (CityDistrictsComponent::PlacedDistrict& district : mutableDistricts->districts) {
            std::vector<BuildingId>::iterator it = std::find(
                district.buildings.begin(), district.buildings.end(),
                POWER_PLANT_DEFS[3].buildingId);
            if (it != district.buildings.end()) {
                district.buildings.erase(it);
                break;
            }
        }
    }

    // Halve population (same as meltdown)
    CityComponent* mutableCity = world.tryGetComponent<CityComponent>(cityEntity);
    if (mutableCity != nullptr && mutableCity->population > 1) {
        mutableCity->population /= 2;
    }

    // Apply fallout: 2-hex radius, 40 turns (double the accidental meltdown)
    applyFalloutRadius(grid, city->location, 2, 40);

    // Massive pollution
    CityPollutionComponent* pollution =
        world.tryGetComponent<CityPollutionComponent>(cityEntity);
    if (pollution != nullptr) {
        pollution->wasteAccumulated += 200;
    }
}

} // namespace aoc::sim
