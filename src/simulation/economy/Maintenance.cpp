/**
 * @file Maintenance.cpp
 * @brief Unit and building maintenance cost processing implementation.
 *
 * All maintenance logic uses the GameState object model (Player/City/Unit).
 * ECS versions have been removed as part of the Phase 3 migration.
 */

#include "aoc/simulation/economy/Maintenance.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/monetary/Inflation.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/core/Log.hpp"

namespace aoc::sim {

CurrencyAmount processGoldIncome(aoc::game::Player& player,
                                  const aoc::map::HexGrid& grid) {
    CurrencyAmount goldIncome = 0;

    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        // Capital Palace bonus (+5 gold, matching Civ 6)
        if (city->isOriginalCapital()) {
            goldIncome += 5;
        }

        // Gold from worked tiles (improvements, terrain, resources)
        for (const aoc::hex::AxialCoord& tile : city->workedTiles()) {
            if (grid.isValid(tile)) {
                aoc::map::TileYield yield = grid.tileYield(grid.toIndex(tile));
                goldIncome += static_cast<CurrencyAmount>(yield.gold);
            }
        }

        // District and building gold bonuses
        const CityDistrictsComponent& districts = city->districts();
        for (const CityDistrictsComponent::PlacedDistrict& d : districts.districts) {
            if (d.type == DistrictType::Commercial) {
                goldIncome += 4;
            }
            if (d.type == DistrictType::Harbor) {
                goldIncome += 2;
            }
            for (BuildingId bid : d.buildings) {
                goldIncome += static_cast<CurrencyAmount>(buildingDef(bid).goldBonus);
            }
        }
    }

    player.addGold(goldIncome);
    player.setIncomePerTurn(goldIncome);
    return goldIncome;
}

void processUnitMaintenance(aoc::game::Player& player) {
    // Per-unit maintenance: each military unit costs gold based on its era.
    // Civilian units (settlers, builders, traders, scouts) are free.
    CurrencyAmount totalMaintenance = 0;
    int32_t paidUnits = 0;

    for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
        int32_t cost = unit->typeDef().maintenanceGold();
        if (cost > 0) {
            totalMaintenance += static_cast<CurrencyAmount>(cost);
            ++paidUnits;
        }
    }

    if (totalMaintenance <= 0) {
        return;
    }

    player.addGold(-totalMaintenance);
    if (paidUnits > 0) {
        LOG_INFO("Player %u unit maintenance: %d military units, cost %lld gold (treasury: %lld)",
                 static_cast<unsigned>(player.id()), paidUnits,
                 static_cast<long long>(totalMaintenance),
                 static_cast<long long>(player.treasury()));
    }

    // Disband most expensive unit if treasury is critically low
    if (player.treasury() < -20) {
        aoc::game::Unit* disbandTarget = nullptr;
        int32_t worstCost = 0;
        for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
            if (unit->typeDef().unitClass == UnitClass::Settler) {
                continue;
            }
            int32_t cost = unit->typeDef().maintenanceGold();
            if (cost > worstCost) {
                worstCost = cost;
                disbandTarget = unit.get();
            }
        }
        if (disbandTarget != nullptr) {
            player.removeUnit(disbandTarget);
            LOG_INFO("Player %u disbanded a unit due to low treasury",
                     static_cast<unsigned>(player.id()));
        }
    }
}

void processBuildingMaintenance(aoc::game::Player& player) {
    CurrencyAmount totalMaintenance = 0;

    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        const CityDistrictsComponent& districts = city->districts();

        // District maintenance: 1 gold per non-CityCenter district
        for (const CityDistrictsComponent::PlacedDistrict& district : districts.districts) {
            if (district.type != DistrictType::CityCenter) {
                totalMaintenance += 1;
            }
            // Building maintenance from building definitions
            for (BuildingId bid : district.buildings) {
                totalMaintenance += static_cast<CurrencyAmount>(buildingDef(bid).maintenanceCost);
            }
        }

        // Per-city maintenance: 2 gold per city beyond the first (empire sprawl)
        if (!city->isOriginalCapital()) {
            totalMaintenance += 2;
        }
    }

    if (totalMaintenance <= 0) {
        return;
    }

    // Scale by inflation price level
    float priceMultiplier = priceLevelMaintenanceMultiplier(
        player.monetary().priceLevel);
    const CurrencyAmount adjustedMaintenance = static_cast<CurrencyAmount>(
        static_cast<float>(totalMaintenance) * priceMultiplier);

    player.addGold(-adjustedMaintenance);
    LOG_INFO("Player %u building/city maintenance: %lld gold (treasury: %lld)",
             static_cast<unsigned>(player.id()),
             static_cast<long long>(adjustedMaintenance),
             static_cast<long long>(player.treasury()));
}

} // namespace aoc::sim
