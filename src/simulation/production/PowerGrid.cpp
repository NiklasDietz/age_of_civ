/**
 * @file PowerGrid.cpp
 * @brief City power grid computation, fuel consumption, and nuclear meltdown.
 */

#include "aoc/simulation/production/PowerGrid.hpp"
#include "aoc/simulation/production/Waste.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/economy/EnergyDependency.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

namespace {

/// Apply fallout to tiles in a hex radius around a city.
void applyFalloutRadius(aoc::map::HexGrid& grid, aoc::hex::AxialCoord center,
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

} // anonymous namespace

CityPowerComponent computeCityPower(aoc::game::GameState& gameState,
                                     const aoc::map::HexGrid& grid,
                                     aoc::game::City& city) {
    CityPowerComponent result{};

    const CityDistrictsComponent& districts = city.districts();
    CityStockpileComponent&       stockpile  = city.stockpile();
    CityPollutionComponent&       pollution  = city.pollution();

    for (const PowerPlantDef& plantDef : POWER_PLANT_DEFS) {
        if (!districts.hasBuilding(plantDef.buildingId)) {
            continue;
        }

        if (plantDef.requiresRiver) {
            bool hasRiver = false;
            const aoc::hex::AxialCoord loc = city.location();
            int32_t cityIndex = grid.toIndex(loc);
            if (grid.isValid(loc) && grid.riverEdges(cityIndex) != 0) {
                hasRiver = true;
            }
            if (!hasRiver) {
                continue;
            }
        }

        if (plantDef.fuelGoodId != 0xFFFF) {
            int32_t available = stockpile.getAmount(plantDef.fuelGoodId);
            if (available < plantDef.fuelPerTurn) {
                continue;
            }
            [[maybe_unused]] bool ok = stockpile.consumeGoods(plantDef.fuelGoodId, plantDef.fuelPerTurn);
        }

        result.energySupply += plantDef.energyOutput;

        if (plantDef.emissions > 0) {
            pollution.co2ContributionPerTurn += plantDef.emissions;
            pollution.wasteAccumulated       += plantDef.emissions;
        }

        if (plantDef.type == PowerPlantType::Nuclear) {
            result.hasNuclear = true;
        }
    }

    for (const CityDistrictsComponent::PlacedDistrict& district : districts.districts) {
        for (BuildingId bid : district.buildings) {
            result.energyDemand += buildingEnergyDemand(bid);
        }
    }

    // Imports: sum of active agreements where this city's owner is the buyer.
    // Capped at ELECTRICITY_IMPORT_CAP_FRACTION of demand per city, so imports
    // complement rather than replace domestic generation. Agreement-wide
    // `lastDeliveredEnergy` is set by `processElectricityAgreements` and
    // already reflects war-break / insufficient-funds skip.
    int32_t rawImport = 0;
    for (const ElectricityAgreementComponent& a : gameState.electricityAgreements()) {
        if (!a.isActive) { continue; }
        if (a.buyer != city.owner()) { continue; }
        rawImport += a.lastDeliveredEnergy;
    }
    if (rawImport > 0 && result.energyDemand > 0) {
        const int32_t cap = static_cast<int32_t>(
            static_cast<float>(result.energyDemand) * ELECTRICITY_IMPORT_CAP_FRACTION);
        const int32_t allowed = (rawImport < cap) ? rawImport : cap;
        result.energySupply += allowed;
    }

    return result;
}

bool checkNuclearMeltdown(aoc::game::GameState& /*gameState*/, aoc::map::HexGrid& grid,
                          aoc::game::City& city, uint32_t turnHash) {
    const CityDistrictsComponent& districts = city.districts();

    bool hasNuclear = false;
    for (const CityDistrictsComponent::PlacedDistrict& district : districts.districts) {
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

    LOG_INFO("NUCLEAR MELTDOWN in city %s! Massive pollution, population halved.",
             city.name().c_str());

    CityDistrictsComponent& mutableDistricts = city.districts();
    for (CityDistrictsComponent::PlacedDistrict& district : mutableDistricts.districts) {
        std::vector<BuildingId>::iterator it = std::find(
            district.buildings.begin(), district.buildings.end(),
            POWER_PLANT_DEFS[3].buildingId);
        if (it != district.buildings.end()) {
            district.buildings.erase(it);
            break;
        }
    }

    city.pollution().wasteAccumulated += 100;

    if (city.population() > 1) {
        city.setPopulation(city.population() / 2);
    }

    applyFalloutRadius(grid, city.location(), 1, 20);
    LOG_INFO("Nuclear fallout applied: 1-hex radius, 20 turns around %s",
             city.name().c_str());

    return true;
}

void applyBombedNuclearFallout(aoc::game::GameState& /*gameState*/, aoc::map::HexGrid& grid,
                                aoc::game::City& city) {
    if (!city.districts().hasBuilding(POWER_PLANT_DEFS[3].buildingId)) {
        return;
    }

    LOG_INFO("NUCLEAR PLANT BOMBED in %s! Massive fallout, 2-hex radius, 40 turns.",
             city.name().c_str());

    CityDistrictsComponent& mutableDistricts = city.districts();
    for (CityDistrictsComponent::PlacedDistrict& district : mutableDistricts.districts) {
        std::vector<BuildingId>::iterator it = std::find(
            district.buildings.begin(), district.buildings.end(),
            POWER_PLANT_DEFS[3].buildingId);
        if (it != district.buildings.end()) {
            district.buildings.erase(it);
            break;
        }
    }

    if (city.population() > 1) {
        city.setPopulation(city.population() / 2);
    }

    applyFalloutRadius(grid, city.location(), 2, 40);

    city.pollution().wasteAccumulated += 200;
}

} // namespace aoc::sim
