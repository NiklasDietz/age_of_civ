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
void applyFalloutRadius(aoc::map::HexGrid& grid, aoc::hex::AxialCoord center, int32_t radius,
                        int16_t durationTurns) {
    std::vector<aoc::hex::AxialCoord> affected;
    affected.reserve(static_cast<std::size_t>(radius * radius * 3 + 1));
    aoc::hex::spiral(center, radius, std::back_inserter(affected));
    affected.push_back(center);

    for (const aoc::hex::AxialCoord& tile : affected) {
        if (!grid.isValid(tile)) {
            continue;
        }
        int32_t idx = grid.toIndex(tile);
        if (aoc::map::isWater(grid.terrain(idx))) {
            continue;
        }
        grid.applyFallout(idx, durationTurns);
    }
}

/// Secure `needed` units of `goodId` for one plant: the city's own stockpile
/// first, then any sibling city in the same empire, then the oil-shock coal
/// substitute. All-or-nothing, so a plant that cannot be fuelled burns nothing.
///
/// This logic used to live in `EconomySimulation::consumeBuildingFuel`, which
/// charged every plant a second time before the grid charged it here: a plant
/// needed twice its listed fuel to run at all (three times for coal, whose two
/// tables disagreed on the amount). The grid is the single owner now, since it
/// is the path that actually gates energy output.
[[nodiscard]] bool secureFuel(aoc::game::Player& player, aoc::game::City& city, uint16_t goodId,
                              int32_t needed, bool inOilShock) {
    CityStockpileComponent& stockpile = city.stockpile();

    const int32_t local = stockpile.getAmount(goodId);
    if (local >= needed) {
        if (!stockpile.consumeGoods(goodId, needed)) {
            LOG_WARN("%s: consumeGoods failed for good %u despite prior "
                     "availability check",
                     city.name().c_str(), static_cast<unsigned>(goodId));
            return false;
        }
        return true;
    }

    // Empire-wide pool across sibling cities. Checked before consuming
    // anything so the "stalled = no fuel burned" contract holds.
    int32_t sibling = 0;
    for (const std::unique_ptr<aoc::game::City>& donorPtr : player.cities()) {
        if (donorPtr == nullptr || donorPtr.get() == &city) {
            continue;
        }
        sibling += donorPtr->stockpile().getAmount(goodId);
    }
    if (local + sibling >= needed) {
        if (local > 0 && !stockpile.consumeGoods(goodId, local)) {
            LOG_WARN("%s: consumeGoods failed for good %u despite prior "
                     "availability check",
                     city.name().c_str(), static_cast<unsigned>(goodId));
            return false;
        }
        int32_t remaining = needed - local;
        for (const std::unique_ptr<aoc::game::City>& donorPtr : player.cities()) {
            if (remaining <= 0) {
                break;
            }
            if (donorPtr == nullptr || donorPtr.get() == &city) {
                continue;
            }
            CityStockpileComponent& donorStock = donorPtr->stockpile();
            const int32_t donorAvail           = donorStock.getAmount(goodId);
            if (donorAvail <= 0) {
                continue;
            }
            const int32_t take = std::min(donorAvail, remaining);
            if (donorStock.consumeGoods(goodId, take)) {
                remaining -= take;
            } else {
                LOG_WARN("%s: consumeGoods failed for good %u despite prior "
                         "availability check",
                         donorPtr->name().c_str(), static_cast<unsigned>(goodId));
            }
        }
        return remaining <= 0;
    }

    // True shortage: an oil burner falls back to coal at 1.5x during a shock.
    if (inOilShock && goodId == goods::OIL) {
        const int32_t coalNeeded = (needed * 3 + 1) / 2;
        if (stockpile.getAmount(goods::COAL) >= coalNeeded &&
            stockpile.consumeGoods(goods::COAL, coalNeeded)) {
            return true;
        }
    }
    return false;
}

} // anonymous namespace

CityPowerComponent computeCityPower(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid,
                                    aoc::game::City& city) {
    CityPowerComponent result{};

    const CityDistrictsComponent& districts = city.districts();
    CityPollutionComponent& pollution       = city.pollution();

    for (const PowerPlantDef& plantDef : POWER_PLANT_DEFS) {
        if (!districts.hasBuilding(plantDef.buildingId)) {
            continue;
        }

        if (plantDef.requiresRiver) {
            bool hasRiver                  = false;
            const aoc::hex::AxialCoord loc = city.location();
            int32_t cityIndex              = grid.toIndex(loc);
            if (grid.isValid(loc) && grid.riverEdges(cityIndex) != 0) {
                hasRiver = true;
            }
            if (!hasRiver) {
                continue;
            }
        }

        if (plantDef.fuelGoodId != 0xFFFF) {
            aoc::game::Player* owner = gameState.player(city.owner());
            if (owner == nullptr) {
                continue;
            }
            if (!secureFuel(*owner, city, plantDef.fuelGoodId, plantDef.fuelPerTurn,
                            owner->energy().inOilShock)) {
                continue; // unfuelled: the plant is offline this turn
            }
        }

        result.energySupply += plantDef.energyOutput;

        if (plantDef.emissions > 0) {
            pollution.co2ContributionPerTurn += plantDef.emissions;
            pollution.wasteAccumulated += plantDef.emissions;
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
        if (!a.isActive) {
            continue;
        }
        if (a.buyer != city.owner()) {
            continue;
        }
        rawImport += a.lastDeliveredEnergy;
    }
    if (rawImport > 0 && result.energyDemand > 0) {
        const int32_t cap     = static_cast<int32_t>(static_cast<float>(result.energyDemand) *
                                                     ELECTRICITY_IMPORT_CAP_FRACTION);
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
        if (hasNuclear) {
            break;
        }
    }
    if (!hasNuclear) {
        return false;
    }

    // 0.2% chance per turn (hash-based deterministic check)
    constexpr uint32_t MELTDOWN_THRESHOLD = 20; // 20/10000 = 0.2%
    uint32_t roll                         = (turnHash * 2654435761u) % 10000;
    if (roll >= MELTDOWN_THRESHOLD) {
        return false;
    }

    LOG_INFO("NUCLEAR MELTDOWN in city %s! Massive pollution, population halved.",
             city.name().c_str());

    CityDistrictsComponent& mutableDistricts = city.districts();
    for (CityDistrictsComponent::PlacedDistrict& district : mutableDistricts.districts) {
        std::vector<BuildingId>::iterator it = std::find(
            district.buildings.begin(), district.buildings.end(), POWER_PLANT_DEFS[3].buildingId);
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
    LOG_INFO("Nuclear fallout applied: 1-hex radius, 20 turns around %s", city.name().c_str());

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
            district.buildings.begin(), district.buildings.end(), POWER_PLANT_DEFS[3].buildingId);
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
