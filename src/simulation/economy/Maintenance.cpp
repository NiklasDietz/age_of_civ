/**
 * @file Maintenance.cpp
 * @brief Unit and building maintenance cost processing implementation.
 *
 * All maintenance logic uses the GameState object model (Player/City/Unit).
 * ECS versions have been removed as part of the Phase 3 migration.
 */

#include "aoc/simulation/economy/Maintenance.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/city/CityScience.hpp"
#include "aoc/simulation/city/DistrictAdjacency.hpp"
#include "aoc/simulation/economy/IndustrialRevolution.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/monetary/FiscalPolicy.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/wonder/Wonder.hpp"
#include "aoc/simulation/map/Improvement.hpp"
#include "aoc/simulation/monetary/Inflation.hpp"
#include "aoc/simulation/government/Government.hpp"
#include "aoc/simulation/government/GovernmentComponent.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/core/Log.hpp"

namespace aoc::sim {

CurrencyAmount goodsTaxCap(int32_t population) {
    // Was a flat 15 for every city, which a modest stockpile reached and then
    // exceeded forever, so every further unit of a finished good was worth
    // nothing. A larger city is a larger market and bears more trade.
    return static_cast<CurrencyAmount>(15 + std::max(0, population) * 3);
}

CurrencyAmount cityGoodsTax(const aoc::game::City& city) {
    const CityStockpileComponent& stock = city.stockpile();
    int32_t gold                        = 0;

    // Everyday goods: plentiful, so taxed lightly per unit.
    gold += stock.getAmount(goods::CONSUMER_GOODS) / 4;
    gold += stock.getAmount(goods::PROCESSED_FOOD) / 4;
    gold += stock.getAmount(goods::CLOTHING) / 2;

    // High-value finished goods. Only ELECTRONICS was counted before, so the
    // most valuable things a civ can make contributed nothing to its economy:
    // Software at a base price of 200 and Microchips at 160 earned zero.
    gold += stock.getAmount(goods::ELECTRONICS);
    gold += stock.getAmount(goods::ADV_CONSUMER_GOODS) * 2;
    gold += stock.getAmount(goods::COMPUTERS_GOOD) * 2;
    gold += stock.getAmount(goods::MICROCHIPS) * 3;
    gold += stock.getAmount(goods::SOFTWARE) * 4;

    return std::min(static_cast<CurrencyAmount>(gold), goodsTaxCap(city.population()));
}

namespace {

constexpr WonderId MACHU_PICCHU{7};
constexpr WonderId BIG_BEN{9};
constexpr uint16_t MARKET         = 6u;
constexpr uint16_t TELECOM_HUB    = 13u;
constexpr uint16_t BANK           = 20u;
constexpr uint16_t STOCK_EXCHANGE = 21u;
constexpr float PALACE_GOLD       = 10.0f;

/// A Barter civ without a coin has no money to tax: it earns nothing and is
/// charged nothing for research.
[[nodiscard]] bool moneyless(const aoc::game::Player& player) {
    return player.monetary().system == MonetarySystemType::Barter &&
           player.monetary().totalCoinCount() == 0;
}

/// Upkeep and stock, tallied the same way for money and barter civs so the
/// diagnostic matches processUnitMaintenance and processBuildingMaintenance.
/// The barter branch used to charge a district fee and double sprawl that no
/// maintenance function ever collected.
void tallyUpkeepAndStock(const aoc::game::Player& player, EconomicBreakdown& bd) {
    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        for (const CityDistrictsComponent::PlacedDistrict& d : city->districts().districts) {
            for (BuildingId bid : d.buildings) {
                bd.expenseBuildings +=
                    static_cast<CurrencyAmount>(buildingDef(bid).maintenanceCost);
            }
        }
        if (!city->isOriginalCapital()) {
            bd.expenseBuildings += 1; // sprawl
        }
        for (const std::pair<const uint16_t, int32_t>& entry : city->stockpile().goods) {
            bd.goodsStockpiled += entry.second;
        }
    }
    for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
        const int32_t cost = unit->typeDef().maintenanceGold();
        if (cost > 0) {
            bd.expenseUnits += static_cast<CurrencyAmount>(cost);
        }
    }
}

/// Coin the civ's Traders brought home this turn. The Trader system credits
/// it on arrival, so it is reported beside the income rather than inside it.
[[nodiscard]] CurrencyAmount routeGoldEarned(const aoc::game::Player& player) {
    CurrencyAmount gold = 0;
    for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
        if (unit->typeDef().unitClass == UnitClass::Trader) {
            gold += unit->trader().goldEarnedThisTurn;
        }
    }
    return gold;
}

[[nodiscard]] aoc::hex::AxialCoord capitalLocation(const aoc::game::Player& player) {
    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        if (city->isOriginalCapital()) {
            return city->location();
        }
    }
    return {0, 0};
}

/// Share of a city's gross gold that survives distance corruption (never at
/// the capital; Communism has none) and the named governor's multiplier.
[[nodiscard]] float cityGoldMultiplier(const aoc::game::City& city, const aoc::map::HexGrid& grid,
                                       aoc::hex::AxialCoord capital, const GovernmentDef& gov) {
    const float governor = city.governor().goldMultiplier();
    if (city.isOriginalCapital() || gov.distanceCorruptionRate <= 0.0f) {
        return governor;
    }
    const float maxDist = static_cast<float>(std::max(grid.width(), grid.height()));
    const float distFraction =
        static_cast<float>(grid.distance(city.location(), capital)) / maxDist;
    const float corruption =
        std::min(gov.corruptionRate + distFraction * gov.distanceCorruptionRate * 0.1f, 0.50f);
    return governor * (1.0f - corruption);
}

[[nodiscard]] bool bordersMountain(const aoc::map::HexGrid& grid, aoc::hex::AxialCoord at) {
    if (!grid.isValid(at)) {
        return false;
    }
    for (const aoc::hex::AxialCoord& n : aoc::hex::neighbors(at)) {
        if (grid.isValid(n) && grid.terrain(grid.toIndex(n)) == aoc::map::TerrainType::Mountain) {
            return true;
        }
    }
    return false;
}

/// Gold of the Market, Bank and Stock Exchange in a city, which Big Ben
/// doubles.
[[nodiscard]] int32_t marketBuildingGold(const aoc::game::City& city) {
    int32_t gold = 0;
    for (const CityDistrictsComponent::PlacedDistrict& d : city.districts().districts) {
        for (BuildingId bid : d.buildings) {
            if (bid.value == MARKET || bid.value == BANK || bid.value == STOCK_EXCHANGE) {
                gold += buildingDef(bid).goldBonus;
            }
        }
    }
    return gold;
}

/// Wonder gold with era decay. Machu Picchu pays only beside a Mountain; Big
/// Ben adds its flat bonus and doubles the market buildings' gold.
[[nodiscard]] float cityWonderGold(const aoc::game::Player& player, const aoc::game::City& city,
                                   const aoc::map::HexGrid& grid) {
    float gold = 0.0f;
    for (const WonderId wid : city.wonders().wonders) {
        if (wid == MACHU_PICCHU && !bordersMountain(grid, city.location())) {
            continue;
        }
        const WonderDef& wdef = wonderDef(wid);
        const float decay     = wonderEraDecayFactor(wdef, player.era().currentEra);
        gold += wdef.effect.goldBonus * decay;
        if (wid == BIG_BEN) {
            gold += static_cast<float>(marketBuildingGold(city)) * decay;
        }
    }
    return gold;
}

/// Hub +3, Harbor +2, every building's goldBonus, and the adjacency yields
/// (river and harbor-side hubs, coastal resources beside a Harbor).
[[nodiscard]] float cityCommercialGold(const aoc::game::City& city, const aoc::map::HexGrid& grid,
                                       const DistrictIndex& index) {
    int32_t gold = 0;
    for (const CityDistrictsComponent::PlacedDistrict& d : city.districts().districts) {
        if (d.type == DistrictType::Commercial) {
            gold += 3;
        }
        if (d.type == DistrictType::Harbor) {
            gold += 2;
        }
        for (BuildingId bid : d.buildings) {
            gold += buildingDef(bid).goldBonus;
        }
    }
    return static_cast<float>(gold) + cityAdjacencyYields(grid, index, city).gold;
}

/// Worked-tile gold including the WP-G improvement cluster bonuses.
[[nodiscard]] int32_t cityTileGold(const aoc::game::City& city, const aoc::map::HexGrid& grid) {
    int32_t gold = 0;
    for (const aoc::hex::AxialCoord& tile : city.workedTiles()) {
        if (grid.isValid(tile)) {
            gold += effectiveTileYield(grid, grid.toIndex(tile)).gold;
        }
    }
    return gold;
}

/// Base 0.50 plus the commerce that makes coin taxable: hub +5%, Market +8%,
/// Bank +12%, Stock Exchange +18%, Telecom Hub +10%, capped at 1.
[[nodiscard]] float coinCollectionEfficiency(const aoc::game::Player& player) {
    float efficiency = 0.50f;
    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        for (const CityDistrictsComponent::PlacedDistrict& d : city->districts().districts) {
            if (d.type == DistrictType::Commercial) {
                efficiency += 0.05f;
            }
            for (BuildingId bid : d.buildings) {
                const uint16_t bv = bid.value;
                efficiency += (bv == MARKET) ? 0.08f : 0.0f;
                efficiency += (bv == BANK) ? 0.12f : 0.0f;
                efficiency += (bv == STOCK_EXCHANGE) ? 0.18f : 0.0f;
                efficiency += (bv == TELECOM_HUB) ? 0.10f : 0.0f;
            }
        }
    }
    return std::min(efficiency, 1.0f);
}

/// Money-supply tax: coin stock x taxable share x tax rate x collection
/// efficiency. The taxable share is a velocity: it turns the coin STOCK into
/// the GDP FLOW a tax rate can apply to, so a hoard is not taxed whole every
/// turn and more coin raises revenue less than proportionally.
[[nodiscard]] float moneySupplyTax(const aoc::game::Player& player) {
    const int32_t supply = player.monetary().totalCoinValue();
    if (supply <= 0) {
        return 0.0f;
    }
    return static_cast<float>(supply) * player.monetary().taxableMoneyShare() *
           player.monetary().taxRate * coinCollectionEfficiency(player);
}

[[nodiscard]] CurrencyAmount toGold(float value) {
    return static_cast<CurrencyAmount>(value);
}

} // namespace

EconomicBreakdown computeEconomicBreakdown(const aoc::game::Player& player,
                                           const aoc::map::HexGrid& grid) {
    EconomicBreakdown bd{};
    tallyUpkeepAndStock(player, bd);
    bd.incomeTradeRoutes = routeGoldEarned(player);
    if (moneyless(player)) {
        bd.totalExpense = bd.expenseUnits + bd.expenseBuildings;
        bd.netFlow      = -bd.totalExpense;
        return bd;
    }

    const GovernmentDef& govDef        = governmentDef(player.government().government);
    const aoc::hex::AxialCoord capital = capitalLocation(player);
    const float indGoldPerCitizen      = player.industrial().cumulativeGoldPerCitizen();
    DistrictIndex districtIndex;
    districtIndex.build(player);

    // Per-city rules, each channel scaled by that city's corruption and governor.
    float capitalGold = 0.0f;
    float headTax     = 0.0f;
    float industrial  = 0.0f;
    float tileGold    = 0.0f;
    float commercial  = 0.0f;
    float goodsTax    = 0.0f;
    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        const float mult = cityGoldMultiplier(*city, grid, capital, govDef);
        const float pop  = static_cast<float>(city->population());
        if (city->isOriginalCapital()) {
            capitalGold += PALACE_GOLD * mult;
        }
        headTax += pop * mult;
        industrial += pop * indGoldPerCitizen * mult;
        tileGold += static_cast<float>(cityTileGold(*city, grid)) * mult;
        commercial +=
            (cityCommercialGold(*city, grid, districtIndex) + cityWonderGold(player, *city, grid)) *
            mult;
        goodsTax += static_cast<float>(cityGoodsTax(*city)) * mult;
    }

    // Civ-wide rules: the money-supply tax, then the government multiplier
    // over every channel. Each channel is truncated on its own so the CSV's
    // channels always sum to its total.
    const float govMult = computeGovernmentModifiers(player.government()).goldMultiplier;
    bd.incomeCapital    = toGold(capitalGold * govMult);
    bd.incomeTax        = toGold(headTax * govMult);
    bd.incomeIndustrial = toGold(industrial * govMult);
    bd.incomeTileGold   = toGold(tileGold * govMult);
    bd.incomeCommercial = toGold(commercial * govMult);
    bd.incomeGoodsEcon  = toGold(goodsTax * govMult);
    bd.incomeMoneyTax   = toGold(moneySupplyTax(player) * govMult);

    // Civ ability: +N gold per active trade route, flat and after the
    // multipliers, like its science and culture siblings in CityScience.cpp.
    const int32_t perRoute = civDef(player.civId()).modifiers.goldFromTradeRoute;
    if (perRoute > 0) {
        bd.incomeCommercial +=
            static_cast<CurrencyAmount>(player.activeTradeRouteCount() * perRoute);
    }

    bd.totalIncome = bd.incomeCapital + bd.incomeTax + bd.incomeIndustrial + bd.incomeTileGold +
                     bd.incomeCommercial + bd.incomeGoodsEcon + bd.incomeMoneyTax;
    bd.effectiveIncome =
        toGold(static_cast<float>(bd.totalIncome) * player.monetary().goldAllocation);
    bd.expenseScience = toGold(computePlayerScience(player, grid) * SCIENCE_FUNDING_COST);
    bd.totalExpense   = bd.expenseUnits + bd.expenseBuildings + bd.expenseScience;
    bd.netFlow        = bd.effectiveIncome - bd.totalExpense;
    return bd;
}

CurrencyAmount processGoldIncome(aoc::game::Player& player, const aoc::map::HexGrid& grid) {
    const EconomicBreakdown bd = computeEconomicBreakdown(player, grid);
    player.addGold(bd.effectiveIncome, aoc::sim::MoneyFlow::unbacked());
    player.setIncomePerTurn(bd.totalIncome);
    return bd.totalIncome;
}

void processUnitMaintenance(aoc::game::Player& player) {
    // Strategic-resource upkeep (always runs regardless of monetary system).
    // Armor/Air/Naval units consume 1 FUEL per turn. Nuclear bombs do not
    // tick (one-shot). If stockpile empty, unit takes attrition damage.
    {
        int32_t fuelCity = -1; // first city found with FUEL stockpile
        // Units don't have direct stockpile — drain from any owned city.
        for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
            if (unit == nullptr) {
                continue;
            }
            const aoc::sim::UnitClass uc = unit->typeDef().unitClass;
            const bool needsFuel =
                (uc == aoc::sim::UnitClass::Armor || uc == aoc::sim::UnitClass::Air ||
                 uc == aoc::sim::UnitClass::Helicopter || uc == aoc::sim::UnitClass::Naval);
            if (!needsFuel) {
                continue;
            }
            // Find a city with FUEL.
            bool drained    = false;
            int32_t cityIdx = 0;
            for (const std::unique_ptr<aoc::game::City>& c : player.cities()) {
                if (c == nullptr) {
                    ++cityIdx;
                    continue;
                }
                if (c->stockpile().getAmount(aoc::sim::goods::FUEL) > 0) {
                    if (c->stockpile().consumeGoods(aoc::sim::goods::FUEL, 1)) {
                        drained  = true;
                        fuelCity = cityIdx;
                        break;
                    }
                    LOG_WARN("Player %u [Maintenance.cpp:processUnitMaintenance] "
                             "consumeGoods failed for good %u at city index %d "
                             "despite prior availability check",
                             static_cast<unsigned>(player.id()),
                             static_cast<unsigned>(aoc::sim::goods::FUEL), cityIdx);
                }
                ++cityIdx;
            }
            if (!drained) {
                // Out of fuel: 5 HP attrition this turn.
                unit->setHitPoints(std::max(1, unit->hitPoints() - 5));
            }
        }
        (void)fuelCity;
    }

    // In barter mode with no coins, money doesn't exist yet.
    // Units are maintained by the city's food/production (not tracked monetarily).
    if (player.monetary().system == MonetarySystemType::Barter &&
        player.monetary().totalCoinCount() == 0) {
        return;
    }

    // Hard floor: the treasury must never drop below -500.  Below this point
    // debt compounds faster than any realistic income can recover it.
    constexpr CurrencyAmount TREASURY_HARD_FLOOR = -500;
    // Threshold at which we switch to military-only mode.
    constexpr CurrencyAmount TREASURY_DEFICIT_LIMIT = 0;
    // Minimum garrison we never disband below.
    constexpr int32_t MIN_GARRISON = 2;
    // Tax rate forced when bankrupt to boost income (below the global ceiling).
    constexpr float BANKRUPT_TAX_RATE = 0.40f;

    // When deeply bankrupt, force maximum tax rate to maximise income recovery.
    if (player.treasury() < TREASURY_HARD_FLOOR) {
        if (player.monetary().taxRate < BANKRUPT_TAX_RATE) {
            setTaxRate(player.monetary(), BANKRUPT_TAX_RATE);
            LOG_WARN("Player %u [Maintenance.cpp:processUnitMaintenance] treasury %lld "
                     "below hard floor -- tax rate forced to %.2f",
                     static_cast<unsigned>(player.id()), static_cast<long long>(player.treasury()),
                     static_cast<double>(BANKRUPT_TAX_RATE));
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
        int32_t worstCost              = 0;
        for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
            if (unit->typeDef().unitClass == UnitClass::Settler) {
                continue;
            }
            const int32_t cost = unit->typeDef().maintenanceGold();
            if (isMilitary(unit->typeDef().unitClass) && cost > worstCost) {
                worstCost     = cost;
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
                    worstCost     = cost;
                    disbandTarget = unit.get();
                }
            }
        }
        if (disbandTarget != nullptr) {
            LOG_WARN("Player %u [Maintenance.cpp:processUnitMaintenance] hard-floor "
                     "bankruptcy (treasury %lld): disbanding %s (cost %d gold/turn)",
                     static_cast<unsigned>(player.id()), static_cast<long long>(player.treasury()),
                     disbandTarget->typeDef().name.data(), worstCost);
            player.removeUnit(disbandTarget);
            --militaryCount;
        }
    }

    // Per-unit maintenance: each military unit costs gold based on its era.
    // Civilian units (settlers, builders, traders, scouts) are free.
    CurrencyAmount totalMaintenance = 0;
    int32_t paidUnits               = 0;

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
                 static_cast<unsigned>(player.id()), static_cast<long long>(player.treasury()),
                 static_cast<long long>(totalMaintenance));
    } else {
        // Treasury is non-negative: pay in full, but apply the hard floor to
        // avoid a single large maintenance bill punching through it.
        const CurrencyAmount afterDeduction = player.treasury() - totalMaintenance;
        if (afterDeduction < TREASURY_HARD_FLOOR) {
            // Partial payment: only deduct down to the floor.
            const CurrencyAmount allowed = player.treasury() - TREASURY_HARD_FLOOR;
            if (allowed > 0) {
                player.addGold(-allowed, aoc::sim::MoneyFlow::unbacked());
            }
            LOG_INFO("Player %u unit maintenance partially paid: %lld of %lld gold "
                     "(hard floor hit, treasury: %lld)",
                     static_cast<unsigned>(player.id()),
                     static_cast<long long>(allowed > 0 ? allowed : 0),
                     static_cast<long long>(totalMaintenance),
                     static_cast<long long>(player.treasury()));
        } else {
            player.addGold(-totalMaintenance, aoc::sim::MoneyFlow::unbacked());
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
    if (player.monetary().consecutiveNegativeTurns >= 5 &&
        player.treasury() < SUSTAINED_THRESHOLD && militaryCount > MIN_GARRISON) {
        aoc::game::Unit* disbandTarget = nullptr;
        int32_t worstCost              = 0;
        for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
            if (unit->typeDef().unitClass == UnitClass::Settler) {
                continue;
            }
            const int32_t cost = unit->typeDef().maintenanceGold();
            if (cost > worstCost) {
                worstCost     = cost;
                disbandTarget = unit.get();
            }
        }
        if (disbandTarget != nullptr) {
            LOG_WARN("Player %u [Maintenance.cpp:processUnitMaintenance] sustained "
                     "bankruptcy (%d turns, treasury %lld): disbanded %s",
                     static_cast<unsigned>(player.id()), player.monetary().consecutiveNegativeTurns,
                     static_cast<long long>(player.treasury()),
                     disbandTarget->typeDef().name.data());
            player.removeUnit(disbandTarget);
            // Reset counter so we don't disband every turn once over the threshold.
            player.monetary().consecutiveNegativeTurns = 0;
        }
    }
}

void processBuildingMaintenance(aoc::game::Player& player) {
    // In barter mode with no coins, money doesn't exist yet — no building upkeep.
    if (player.monetary().system == MonetarySystemType::Barter &&
        player.monetary().totalCoinCount() == 0) {
        return;
    }

    // Skip all building and district maintenance when the treasury is already
    // deeply in debt.  City-center upkeep (the +2 per city sprawl cost) is
    // also deferred -- the unit maintenance hard floor is the primary recovery
    // mechanism at this point.
    constexpr CurrencyAmount SKIP_THRESHOLD = -200;
    if (player.treasury() < SKIP_THRESHOLD) {
        LOG_INFO("Player %u building/city maintenance skipped (treasury %lld < %lld)",
                 static_cast<unsigned>(player.id()), static_cast<long long>(player.treasury()),
                 static_cast<long long>(SKIP_THRESHOLD));
        return;
    }

    CurrencyAmount totalMaintenance = 0;

    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        const CityDistrictsComponent& districts = city->districts();

        // Building maintenance from building definitions only.
        // Districts themselves no longer add flat maintenance — their upkeep
        // is embodied in the buildings inside them.  Removing the district flat
        // fee reduces the early-game maintenance burden so players can sustain
        // an empire while the coin economy is still bootstrapping.
        for (const CityDistrictsComponent::PlacedDistrict& district : districts.districts) {
            for (BuildingId bid : district.buildings) {
                totalMaintenance += static_cast<CurrencyAmount>(buildingDef(bid).maintenanceCost);
            }
        }

        // Per-city maintenance: 1 gold per city beyond the first (empire sprawl).
        // Reduced from 2 to 1 — sprawl costs are still present but less punishing.
        if (!city->isOriginalCapital()) {
            totalMaintenance += 1;
        }
    }

    if (totalMaintenance <= 0) {
        return;
    }

    // Scale by inflation price level.
    const float priceMultiplier = priceLevelMaintenanceMultiplier(player.monetary().priceLevel);
    const CurrencyAmount adjustedMaintenance =
        static_cast<CurrencyAmount>(static_cast<float>(totalMaintenance) * priceMultiplier);

    // Apply the hard floor: never let a single maintenance tick punch the
    // treasury below -500.
    constexpr CurrencyAmount TREASURY_HARD_FLOOR = -500;
    const CurrencyAmount afterDeduction          = player.treasury() - adjustedMaintenance;
    if (afterDeduction < TREASURY_HARD_FLOOR) {
        const CurrencyAmount allowed = player.treasury() - TREASURY_HARD_FLOOR;
        if (allowed > 0) {
            player.addGold(-allowed, aoc::sim::MoneyFlow::unbacked());
        }
        LOG_INFO(
            "Player %u building/city maintenance partially paid: %lld of %lld gold "
            "(hard floor hit, treasury: %lld)",
            static_cast<unsigned>(player.id()), static_cast<long long>(allowed > 0 ? allowed : 0),
            static_cast<long long>(adjustedMaintenance), static_cast<long long>(player.treasury()));
    } else {
        player.addGold(-adjustedMaintenance, aoc::sim::MoneyFlow::unbacked());
        LOG_INFO("Player %u building/city maintenance: %lld gold (treasury: %lld)",
                 static_cast<unsigned>(player.id()), static_cast<long long>(adjustedMaintenance),
                 static_cast<long long>(player.treasury()));
    }
}

void processMilitaryFoodConsumption(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid,
                                    aoc::game::Player& player) {
    // A civilization without a city forages: no demand, no desertion. The
    // escort of a still-walking settler used to desert on turn 5 (2026-09-04
    // Tutorial, finding 1), leaving the settler alone to die of attrition.
    if (player.cities().empty()) {
        for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
            if (unit->turnsStarving() > 0) {
                unit->setTurnsStarving(0);
            }
        }
        return;
    }

    // WP-P1: aggregate food demand across all military / mounted / armor units.
    // WP-Q: only units OUTSIDE owned territory drain stockpile. Garrison
    // forages locally (zero cost). Expeditionary forces need supply lines.
    int32_t demand   = 0;
    int32_t fed      = 0;
    int32_t starving = 0;
    for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
        if (unit->typeDef().foodPerTurn() <= 0) {
            continue;
        }
        const int32_t pIdx = grid.toIndex(unit->position());
        if (grid.owner(pIdx) == player.id()) {
            // Reset starving on garrison (returning home heals food state).
            if (unit->turnsStarving() > 0) {
                unit->setTurnsStarving(0);
            }
            continue;
        }
        demand += unit->typeDef().foodPerTurn();
    }
    if (demand <= 0) {
        return;
    }

    // WP-S: lazy-seed Encampment buffers for any owned encampment improvement
    // that has no buffer entry yet (100 food + 100 fuel seed). Also auto-
    // refill 5 food + 5 fuel per turn from owner's nearest city stockpile.
    {
        const int32_t tilesN = grid.tileCount();
        for (int32_t ti = 0; ti < tilesN; ++ti) {
            if (grid.improvement(ti) != aoc::map::ImprovementType::Encampment) {
                continue;
            }
            if (grid.owner(ti) != player.id()) {
                continue;
            }
            std::unordered_map<int32_t, aoc::game::GameState::EncampmentBuffer>::iterator it =
                gameState.encampments().find(ti);
            if (it == gameState.encampments().end()) {
                aoc::game::GameState::EncampmentBuffer buf;
                buf.owner = player.id();
                buf.food  = 100;
                buf.fuel  = 100;
                gameState.encampments().emplace(ti, buf);
                continue;
            }
            // WP-S2 lite: auto-refill if buffers below cap (100/100).
            // Drains 5 food + 5 fuel from nearest owned city's stockpile.
            constexpr int32_t REFILL_RATE    = 5;
            constexpr int32_t CAP            = 100;
            const aoc::hex::AxialCoord depot = grid.toAxial(ti);
            aoc::game::City* nearest         = nullptr;
            int32_t bestDist                 = std::numeric_limits<int32_t>::max();
            for (const std::unique_ptr<aoc::game::City>& c : player.cities()) {
                if (c == nullptr) {
                    continue;
                }
                const int32_t d = grid.distance(depot, c->location());
                if (d < bestDist) {
                    bestDist = d;
                    nearest  = c.get();
                }
            }
            if (nearest == nullptr) {
                continue;
            }
            CityStockpileComponent& sp = nearest->stockpile();
            if (it->second.food < CAP) {
                const int32_t want = std::min(REFILL_RATE, CAP - it->second.food);
                int32_t got        = 0;
                for (uint16_t gid : {goods::PROCESSED_FOOD, goods::WHEAT, goods::CATTLE,
                                     goods::FISH, goods::RICE}) {
                    if (got >= want) {
                        break;
                    }
                    const int32_t avail = sp.getAmount(gid);
                    if (avail <= 0) {
                        continue;
                    }
                    const int32_t take = std::min(avail, want - got);
                    if (sp.consumeGoods(gid, take)) {
                        got += take;
                    }
                }
                it->second.food += got;
            }
            if (it->second.fuel < CAP) {
                const int32_t want = std::min(REFILL_RATE, CAP - it->second.fuel);
                int32_t got        = 0;
                for (uint16_t gid : {goods::FUEL, goods::COAL}) {
                    if (got >= want) {
                        break;
                    }
                    const int32_t avail = sp.getAmount(gid);
                    if (avail <= 0) {
                        continue;
                    }
                    const int32_t take = std::min(avail, want - got);
                    if (sp.consumeGoods(gid, take)) {
                        got += take;
                    }
                }
                it->second.fuel += got;
            }
        }
    }

    // WP-S: drain encampment buffers first for units within 5 hex.
    int32_t remaining = demand;
    {
        for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
            const int32_t cost = unit->typeDef().foodPerTurn();
            if (cost <= 0) {
                continue;
            }
            for (std::pair<const int32_t, aoc::game::GameState::EncampmentBuffer>& kv :
                 gameState.encampments()) {
                if (kv.second.owner != player.id()) {
                    continue;
                }
                if (kv.second.food <= 0) {
                    continue;
                }
                const aoc::hex::AxialCoord depot = grid.toAxial(kv.first);
                if (grid.distance(unit->position(), depot) > 5) {
                    continue;
                }
                const int32_t take = std::min(cost, kv.second.food);
                kv.second.food -= take;
                remaining -= take;
                break;
            }
        }
    }

    // Drain priority order: processed first (most efficient), then raw foods.
    constexpr std::array<uint16_t, 5> FOOD_GOODS = {goods::PROCESSED_FOOD, goods::WHEAT,
                                                    goods::CATTLE, goods::FISH, goods::RICE};

    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        if (remaining <= 0) {
            break;
        }
        CityStockpileComponent& sp = city->stockpile();
        for (uint16_t goodId : FOOD_GOODS) {
            if (remaining <= 0) {
                break;
            }
            const int32_t avail = sp.getAmount(goodId);
            if (avail <= 0) {
                continue;
            }
            const int32_t take = std::min(avail, remaining);
            (void)sp.consumeGoods(goodId, take);
            remaining -= take;
        }
    }
    fed = demand - remaining;

    // Per-unit starve tracking. Distribute satisfied food first to weakest
    // (highest foodPerTurn) units so heavy armor doesn't bypass infantry.
    // Only units outside own territory are tracked (garrison eats local).
    std::vector<aoc::game::Unit*> sorted;
    sorted.reserve(player.units().size());
    for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
        if (unit->typeDef().foodPerTurn() <= 0) {
            continue;
        }
        const int32_t pIdx = grid.toIndex(unit->position());
        if (grid.owner(pIdx) == player.id()) {
            continue;
        }
        sorted.push_back(unit.get());
    }
    std::sort(sorted.begin(), sorted.end(), [](const aoc::game::Unit* a, const aoc::game::Unit* b) {
        return a->typeDef().foodPerTurn() < b->typeDef().foodPerTurn();
    });

    int32_t budget = fed;
    for (aoc::game::Unit* unit : sorted) {
        const int32_t cost = unit->typeDef().foodPerTurn();
        if (budget >= cost) {
            budget -= cost;
            if (unit->turnsStarving() > 0) {
                unit->setTurnsStarving(0);
            }
        } else {
            unit->incrementStarving();
            ++starving;
        }
    }

    // 5+ consecutive starving turns → auto-disband oldest starving unit.
    aoc::game::Unit* disbandTarget = nullptr;
    int32_t worst                  = 0;
    for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
        if (unit->turnsStarving() >= 5 && unit->turnsStarving() > worst) {
            worst         = unit->turnsStarving();
            disbandTarget = unit.get();
        }
    }
    if (disbandTarget != nullptr) {
        LOG_WARN("Player %u unit '%.*s' deserted after %d starving turns",
                 static_cast<unsigned>(player.id()),
                 static_cast<int>(disbandTarget->typeDef().name.size()),
                 disbandTarget->typeDef().name.data(), disbandTarget->turnsStarving());
        player.removeUnit(disbandTarget);
    }

    // Food-upkeep log silenced 2026-05-02 — was firing per turn per
    // civ with deficit, producing 200+ "starving" lines per sim. The
    // mechanic still drives unit desertion (logged separately on
    // disband). Famine as a STANDALONE event is gated to drought
    // disasters only (WorldEventId::FamineWarning fires on globalTemp
    // ≥ 2.0); routine food deficit is silent.
    (void)demand;
    (void)fed;
    (void)starving;
}

} // namespace aoc::sim
