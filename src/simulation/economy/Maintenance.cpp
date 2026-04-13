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

        // Population-based tax income: 1 gold per 2 citizens (baseline taxation)
        cityGold += static_cast<CurrencyAmount>(city->population() / 2);

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
    // Hard floor: the treasury must never drop below -500.  Below this point
    // debt compounds faster than any realistic income can recover it.
    constexpr CurrencyAmount TREASURY_HARD_FLOOR    = -500;
    // Threshold at which we switch to military-only mode.
    constexpr CurrencyAmount TREASURY_DEFICIT_LIMIT = 0;
    // Minimum garrison we never disband below.
    constexpr int32_t        MIN_GARRISON           = 2;
    // Maximum tax rate applied automatically when bankrupt to boost income.
    constexpr float          MAX_TAX_RATE           = 0.40f;

    // When deeply bankrupt, force maximum tax rate to maximise income recovery.
    if (player.treasury() < TREASURY_HARD_FLOOR) {
        if (player.monetary().taxRate < MAX_TAX_RATE) {
            player.monetary().taxRate = MAX_TAX_RATE;
            LOG_WARN("Player %u [Maintenance.cpp:processUnitMaintenance] treasury %lld "
                     "below hard floor -- tax rate forced to %.2f",
                     static_cast<unsigned>(player.id()),
                     static_cast<long long>(player.treasury()),
                     static_cast<double>(MAX_TAX_RATE));
        }
    }

    // Count military units before any potential disband.
    int32_t militaryCount = 0;
    for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
        if (isMilitary(unit->typeDef().unitClass)) {
            ++militaryCount;
        }
    }

    // Aggressive disband at the hard floor: remove the most expensive military
    // unit immediately so the treasury stops bleeding.
    if (player.treasury() < TREASURY_HARD_FLOOR && militaryCount > MIN_GARRISON) {
        aoc::game::Unit* disbandTarget = nullptr;
        int32_t worstCost = 0;
        for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
            if (unit->typeDef().unitClass == UnitClass::Settler) {
                continue;
            }
            const int32_t cost = unit->typeDef().maintenanceGold();
            if (isMilitary(unit->typeDef().unitClass) && cost > worstCost) {
                worstCost    = cost;
                disbandTarget = unit.get();
            }
        }
        // Fall back to any non-settler unit if no military candidate found.
        if (disbandTarget == nullptr) {
            for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
                if (unit->typeDef().unitClass == UnitClass::Settler) {
                    continue;
                }
                const int32_t cost = unit->typeDef().maintenanceGold();
                if (cost > worstCost) {
                    worstCost    = cost;
                    disbandTarget = unit.get();
                }
            }
        }
        if (disbandTarget != nullptr) {
            LOG_WARN("Player %u [Maintenance.cpp:processUnitMaintenance] hard-floor "
                     "bankruptcy (treasury %lld): disbanding %s (cost %d gold/turn)",
                     static_cast<unsigned>(player.id()),
                     static_cast<long long>(player.treasury()),
                     disbandTarget->typeDef().name.data(),
                     worstCost);
            player.removeUnit(disbandTarget);
            --militaryCount;
        }
    }

    // Per-unit maintenance: each military unit costs gold based on its era.
    // Civilian units (settlers, builders, traders, scouts) are free.
    CurrencyAmount totalMaintenance = 0;
    int32_t paidUnits = 0;

    for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
        const int32_t cost = unit->typeDef().maintenanceGold();
        if (cost > 0) {
            totalMaintenance += static_cast<CurrencyAmount>(cost);
            ++paidUnits;
        }
    }

    if (totalMaintenance <= 0) {
        return;
    }

    if (player.treasury() < TREASURY_DEFICIT_LIMIT) {
        // Already in deficit: skip ALL unit maintenance -- paying it would push
        // the treasury further negative and trigger the hard floor faster.
        // The disband logic above already sheds the most expensive unit each
        // turn, which is the correct pressure relief mechanism.
        LOG_INFO("Player %u unit maintenance skipped (treasury %lld < 0): "
                 "would have cost %lld gold",
                 static_cast<unsigned>(player.id()),
                 static_cast<long long>(player.treasury()),
                 static_cast<long long>(totalMaintenance));
    } else {
        // Treasury is non-negative: pay in full, but apply the hard floor to
        // avoid a single large maintenance bill punching through it.
        const CurrencyAmount afterDeduction = player.treasury() - totalMaintenance;
        if (afterDeduction < TREASURY_HARD_FLOOR) {
            // Partial payment: only deduct down to the floor.
            const CurrencyAmount allowed = player.treasury() - TREASURY_HARD_FLOOR;
            if (allowed > 0) {
                player.addGold(-allowed);
            }
            LOG_INFO("Player %u unit maintenance partially paid: %lld of %lld gold "
                     "(hard floor hit, treasury: %lld)",
                     static_cast<unsigned>(player.id()),
                     static_cast<long long>(allowed > 0 ? allowed : 0),
                     static_cast<long long>(totalMaintenance),
                     static_cast<long long>(player.treasury()));
        } else {
            player.addGold(-totalMaintenance);
            LOG_INFO("Player %u unit maintenance: %d units, cost %lld gold "
                     "(treasury: %lld)",
                     static_cast<unsigned>(player.id()), paidUnits,
                     static_cast<long long>(totalMaintenance),
                     static_cast<long long>(player.treasury()));
        }
    }

    // Update consecutive negative-treasury counter.
    if (player.treasury() < 0) {
        ++player.monetary().consecutiveNegativeTurns;
    } else {
        player.monetary().consecutiveNegativeTurns = 0;
    }

    // Sustained bankruptcy (>= 5 consecutive turns below -200): disband the
    // most expensive unit, still respecting the minimum garrison.
    constexpr CurrencyAmount SUSTAINED_THRESHOLD = -200;
    if (player.monetary().consecutiveNegativeTurns >= 5
        && player.treasury() < SUSTAINED_THRESHOLD
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
                worstCost    = cost;
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
            // Reset counter so we don't disband every turn once over the threshold.
            player.monetary().consecutiveNegativeTurns = 0;
        }
    }
}

void processBuildingMaintenance(aoc::game::Player& player) {
    // Skip all building and district maintenance when the treasury is already
    // deeply in debt.  City-center upkeep (the +2 per city sprawl cost) is
    // also deferred -- the unit maintenance hard floor is the primary recovery
    // mechanism at this point.
    constexpr CurrencyAmount SKIP_THRESHOLD = -200;
    if (player.treasury() < SKIP_THRESHOLD) {
        LOG_INFO("Player %u building/city maintenance skipped (treasury %lld < %lld)",
                 static_cast<unsigned>(player.id()),
                 static_cast<long long>(player.treasury()),
                 static_cast<long long>(SKIP_THRESHOLD));
        return;
    }

    CurrencyAmount totalMaintenance = 0;

    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        const CityDistrictsComponent& districts = city->districts();

        // District maintenance: 1 gold per non-CityCenter district.
        for (const CityDistrictsComponent::PlacedDistrict& district : districts.districts) {
            if (district.type != DistrictType::CityCenter) {
                totalMaintenance += 1;
            }
            // Building maintenance from building definitions.
            for (BuildingId bid : district.buildings) {
                totalMaintenance += static_cast<CurrencyAmount>(buildingDef(bid).maintenanceCost);
            }
        }

        // Per-city maintenance: 2 gold per city beyond the first (empire sprawl).
        if (!city->isOriginalCapital()) {
            totalMaintenance += 2;
        }
    }

    if (totalMaintenance <= 0) {
        return;
    }

    // Scale by inflation price level.
    const float priceMultiplier = priceLevelMaintenanceMultiplier(
        player.monetary().priceLevel);
    const CurrencyAmount adjustedMaintenance = static_cast<CurrencyAmount>(
        static_cast<float>(totalMaintenance) * priceMultiplier);

    // Apply the hard floor: never let a single maintenance tick punch the
    // treasury below -500.
    constexpr CurrencyAmount TREASURY_HARD_FLOOR = -500;
    const CurrencyAmount afterDeduction = player.treasury() - adjustedMaintenance;
    if (afterDeduction < TREASURY_HARD_FLOOR) {
        const CurrencyAmount allowed = player.treasury() - TREASURY_HARD_FLOOR;
        if (allowed > 0) {
            player.addGold(-allowed);
        }
        LOG_INFO("Player %u building/city maintenance partially paid: %lld of %lld gold "
                 "(hard floor hit, treasury: %lld)",
                 static_cast<unsigned>(player.id()),
                 static_cast<long long>(allowed > 0 ? allowed : 0),
                 static_cast<long long>(adjustedMaintenance),
                 static_cast<long long>(player.treasury()));
    } else {
        player.addGold(-adjustedMaintenance);
        LOG_INFO("Player %u building/city maintenance: %lld gold (treasury: %lld)",
                 static_cast<unsigned>(player.id()),
                 static_cast<long long>(adjustedMaintenance),
                 static_cast<long long>(player.treasury()));
    }
}

} // namespace aoc::sim
