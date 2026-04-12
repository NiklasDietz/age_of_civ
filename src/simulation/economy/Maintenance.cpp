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
#include "aoc/simulation/government/Government.hpp"
#include "aoc/simulation/government/GovernmentComponent.hpp"
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

    // Find capital location for distance-based corruption
    aoc::hex::AxialCoord capitalLocation{0, 0};
    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        if (city->isOriginalCapital()) {
            capitalLocation = city->location();
            break;
        }
    }

    // Get government corruption rates
    const aoc::sim::GovernmentDef& govDef = governmentDef(player.government().government);

    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        CurrencyAmount cityGold = 0;

        // Capital Palace bonus (+5 gold)
        if (city->isOriginalCapital()) {
            cityGold += 5;
        }

        // Gold from worked tiles
        for (const aoc::hex::AxialCoord& tile : city->workedTiles()) {
            if (grid.isValid(tile)) {
                aoc::map::TileYield yield = grid.tileYield(grid.toIndex(tile));
                cityGold += static_cast<CurrencyAmount>(yield.gold);
            }
        }

        // Specialist taxmen: +3 gold each (read from ECS sync'd data or default 0)
        // Taxmen gold is handled here since we compute per-city gold

        // District and building gold bonuses
        const CityDistrictsComponent& districts = city->districts();
        for (const CityDistrictsComponent::PlacedDistrict& d : districts.districts) {
            if (d.type == DistrictType::Commercial) {
                cityGold += 4;
            }
            if (d.type == DistrictType::Harbor) {
                cityGold += 2;
            }
            for (BuildingId bid : d.buildings) {
                cityGold += static_cast<CurrencyAmount>(buildingDef(bid).goldBonus);
            }
        }

        // Distance-based corruption: reduces gold based on distance from capital.
        // Varies by government type (Communism has 0 distance corruption).
        if (!city->isOriginalCapital() && govDef.distanceCorruptionRate > 0.0f) {
            int32_t dist = aoc::hex::distance(city->location(), capitalLocation);
            float maxDist = static_cast<float>(std::max(grid.width(), grid.height()));
            float distFraction = static_cast<float>(dist) / maxDist;
            float corruptionPct = govDef.corruptionRate + distFraction * govDef.distanceCorruptionRate * 0.1f;
            corruptionPct = std::min(corruptionPct, 0.50f);  // Cap at 50%
            cityGold = static_cast<CurrencyAmount>(
                static_cast<float>(cityGold) * (1.0f - corruptionPct));
        }

        goldIncome += cityGold;
    }

    // Apply gold allocation slider: only the gold fraction goes to treasury.
    // The rest is allocated to science and luxury bonuses (handled in their respective systems).
    CurrencyAmount effectiveGold = static_cast<CurrencyAmount>(
        static_cast<float>(goldIncome) * player.monetary().goldAllocation);
    player.addGold(effectiveGold);
    player.setIncomePerTurn(goldIncome);  // Display full income before split
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

    // Spending brake: if treasury is already negative, skip non-essential
    // (non-military) maintenance. Military must still be paid.
    if (player.treasury() < 0) {
        CurrencyAmount militaryOnly = 0;
        for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
            if (isMilitary(unit->typeDef().unitClass)) {
                militaryOnly += static_cast<CurrencyAmount>(unit->typeDef().maintenanceGold());
            }
        }
        // Only pay military maintenance when already in deficit
        player.addGold(-militaryOnly);
        LOG_INFO("Player %u unit maintenance (deficit mode): %lld gold military only "
                 "(treasury: %lld)",
                 static_cast<unsigned>(player.id()),
                 static_cast<long long>(militaryOnly),
                 static_cast<long long>(player.treasury()));
    } else {
        player.addGold(-totalMaintenance);
        if (paidUnits > 0) {
            LOG_INFO("Player %u unit maintenance: %d military units, cost %lld gold "
                     "(treasury: %lld)",
                     static_cast<unsigned>(player.id()), paidUnits,
                     static_cast<long long>(totalMaintenance),
                     static_cast<long long>(player.treasury()));
        }
    }

    // Update consecutive negative-treasury counter
    if (player.treasury() < 0) {
        ++player.monetary().consecutiveNegativeTurns;
    } else {
        player.monetary().consecutiveNegativeTurns = 0;
    }

    // Count current military units to enforce the minimum garrison.
    int32_t militaryCount = 0;
    for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
        if (isMilitary(unit->typeDef().unitClass)) {
            ++militaryCount;
        }
    }
    constexpr int32_t MIN_GARRISON = 2;

    // Disband most expensive military unit on immediate bankruptcy, but only
    // when the player has more than the minimum garrison.  Treasury threshold is
    // -200 to avoid wiping units from a brief gold dip.
    if (player.treasury() < -200 && militaryCount > MIN_GARRISON) {
        aoc::game::Unit* disbandTarget = nullptr;
        int32_t worstCost = 0;
        for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
            if (unit->typeDef().unitClass == UnitClass::Settler) {
                continue;
            }
            const bool isMil = isMilitary(unit->typeDef().unitClass);
            const int32_t cost = unit->typeDef().maintenanceGold();
            // Prefer disbanding military units first (highest gold cost)
            if (isMil && cost > worstCost) {
                worstCost = cost;
                disbandTarget = unit.get();
            }
        }
        // Fall back to any non-settler, non-military unit if no military candidate
        if (disbandTarget == nullptr) {
            for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
                if (unit->typeDef().unitClass == UnitClass::Settler) {
                    continue;
                }
                if (isMilitary(unit->typeDef().unitClass)) {
                    continue;
                }
                const int32_t cost = unit->typeDef().maintenanceGold();
                if (cost > worstCost) {
                    worstCost = cost;
                    disbandTarget = unit.get();
                }
            }
        }
        if (disbandTarget != nullptr) {
            LOG_WARN("Player %u [Maintenance.cpp:processUnitMaintenance] bankrupt: "
                     "disbanded %s (cost %d gold/turn, treasury %lld)",
                     static_cast<unsigned>(player.id()),
                     disbandTarget->typeDef().name.data(),
                     worstCost,
                     static_cast<long long>(player.treasury()));
            player.removeUnit(disbandTarget);
        }
    }

    // Sustained bankruptcy (>= 5 turns below -200): disband most expensive unit,
    // still respecting the minimum garrison.
    if (player.monetary().consecutiveNegativeTurns >= 5
        && player.treasury() < -200
        && militaryCount > MIN_GARRISON)
    {
        aoc::game::Unit* disbandTarget = nullptr;
        int32_t worstCost = 0;
        for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
            if (unit->typeDef().unitClass == UnitClass::Settler) {
                continue;
            }
            const int32_t cost = unit->typeDef().maintenanceGold();
            if (cost > worstCost) {
                worstCost = cost;
                disbandTarget = unit.get();
            }
        }
        if (disbandTarget != nullptr) {
            LOG_WARN("Player %u [Maintenance.cpp:processUnitMaintenance] sustained "
                     "bankruptcy (%d turns, treasury %lld): disbanded %s",
                     static_cast<unsigned>(player.id()),
                     player.monetary().consecutiveNegativeTurns,
                     static_cast<long long>(player.treasury()),
                     disbandTarget->typeDef().name.data());
            player.removeUnit(disbandTarget);
            // Reset counter so we don't disband every turn at exactly the threshold
            player.monetary().consecutiveNegativeTurns = 0;
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
