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
#include "aoc/simulation/city/CityConnection.hpp"
#include "aoc/simulation/city/DistrictAdjacency.hpp"
#include "aoc/simulation/economy/IndustrialRevolution.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/monetary/FiscalPolicy.hpp"
#include "aoc/simulation/monetary/MoneyFlow.hpp"
#include "aoc/simulation/citystate/CityState.hpp"
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

#include <map>

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

// Collection-efficiency contributions (plan 2.3).
constexpr float PALACE_EFFICIENCY     = 0.10f;
constexpr float HUB_EFFICIENCY        = 0.05f;
constexpr float HARBOR_EFFICIENCY     = 0.03f;
constexpr float GOLD_POINT_EFFICIENCY = 0.01f; ///< per point of tile, adjacency or goods gold
constexpr float CITY_GOLD_CAP         = 0.10f; ///< cap on a city's tile, adjacency and goods gold
constexpr float WONDER_GOLD_POINT     = 0.02f;
constexpr float INDUSTRIAL_POINT      = 0.02f; ///< per point of gold per citizen from industrialisation
constexpr float CONNECTION_EFFICIENCY = 0.02f; ///< per city connected to the capital by road
constexpr float ROUTE_ABILITY_POINT   = 0.01f; ///< per point of a civ's goldFromTradeRoute, per route

/// Upkeep and stock, tallied the same way for money and barter civs so the
/// diagnostic matches processUnitMaintenance and processBuildingMaintenance,
/// nominal at the civ's price level.
void tallyUpkeepAndStock(const aoc::game::Player& player, EconomicBreakdown& bd) {
    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        if (city == nullptr || city->owner() != player.id()) {
            continue;
        }
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
    const float priceMult = priceLevelMaintenanceMultiplier(player.monetary().priceLevel);
    bd.expenseUnits     = static_cast<CurrencyAmount>(static_cast<float>(bd.expenseUnits) * priceMult);
    bd.expenseBuildings = static_cast<CurrencyAmount>(static_cast<float>(bd.expenseBuildings) * priceMult);
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

/// Share of a city's commerce that survives distance corruption (never at
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

/// The Market, Bank and Stock Exchange contributions in a city, which Big
/// Ben doubles.
[[nodiscard]] float marketBuildingBonus(const aoc::game::City& city) {
    float bonus = 0.0f;
    for (const CityDistrictsComponent::PlacedDistrict& d : city.districts().districts) {
        for (BuildingId bid : d.buildings) {
            if (bid.value == MARKET || bid.value == BANK || bid.value == STOCK_EXCHANGE) {
                bonus += buildingCollectionBonus(bid);
            }
        }
    }
    return bonus;
}

/// Wonder gold points with era decay, plus Big Ben's doubling of the market
/// buildings. Machu Picchu pays only beside a Mountain.
[[nodiscard]] float cityWonderBonus(const aoc::game::Player& player, const aoc::game::City& city,
                                    const aoc::map::HexGrid& grid) {
    float bonus = 0.0f;
    for (const WonderId wid : city.wonders().wonders) {
        if (wid == MACHU_PICCHU && !bordersMountain(grid, city.location())) {
            continue;
        }
        const WonderDef& wdef = wonderDef(wid);
        const float decay     = wonderEraDecayFactor(wdef, player.era().currentEra);
        bonus += wdef.effect.goldBonus * decay * WONDER_GOLD_POINT;
        if (wid == BIG_BEN) {
            bonus += marketBuildingBonus(city) * decay;
        }
    }
    return bonus;
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

/// One city's commerce as collection efficiency, before corruption and the
/// governor: Palace, hubs and harbors, buildings, the gold its tiles, its
/// adjacency and its finished goods yield (capped), its wonders and its
/// industrialisation.
[[nodiscard]] float cityCommerceBonus(const aoc::game::Player& player, const aoc::game::City& city,
                                      const aoc::map::HexGrid& grid, const DistrictIndex& index) {
    float bonus = city.isOriginalCapital() ? PALACE_EFFICIENCY : 0.0f;
    for (const CityDistrictsComponent::PlacedDistrict& d : city.districts().districts) {
        if (d.type == DistrictType::Commercial) {
            bonus += HUB_EFFICIENCY;
        }
        if (d.type == DistrictType::Harbor) {
            bonus += HARBOR_EFFICIENCY;
        }
        for (BuildingId bid : d.buildings) {
            bonus += buildingCollectionBonus(bid);
        }
    }
    const float goldPoints = static_cast<float>(cityTileGold(city, grid)) +
                             cityAdjacencyYields(grid, index, city).gold +
                             static_cast<float>(cityGoodsTax(city));
    bonus += std::min(CITY_GOLD_CAP, goldPoints * GOLD_POINT_EFFICIENCY);
    bonus += cityWonderBonus(player, city, grid);
    bonus += player.industrial().cumulativeGoldPerCitizen() * INDUSTRIAL_POINT;
    return bonus;
}

[[nodiscard]] CurrencyAmount toGold(float value) {
    return static_cast<CurrencyAmount>(value);
}

} // namespace

float buildingCollectionBonus(BuildingId building) {
    switch (building.value) {
        case MARKET:         return 0.08f;
        case BANK:           return 0.12f;
        case STOCK_EXCHANGE: return 0.18f;
        case TELECOM_HUB:    return 0.10f;
        default:             return static_cast<float>(buildingDef(building).goldBonus) * GOLD_POINT_EFFICIENCY;
    }
}

float collectionEfficiency(const aoc::game::Player& player, const aoc::map::HexGrid& grid,
                           float allianceGoldMult) {
    const GovernmentDef& govDef        = governmentDef(player.government().government);
    const aoc::hex::AxialCoord capital = capitalLocation(player);
    DistrictIndex districtIndex;
    districtIndex.build(player);

    float totalPop = 0.0f;
    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        if (city != nullptr && city->owner() == player.id()) {
            totalPop += static_cast<float>(std::max(0, city->population()));
        }
    }
    float efficiency = BASE_COLLECTION_EFFICIENCY;
    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        if (city == nullptr || city->owner() != player.id() || totalPop <= 0.0f) {
            continue;
        }
        const float weight = static_cast<float>(std::max(0, city->population())) / totalPop;
        efficiency += weight * cityCommerceBonus(player, *city, grid, districtIndex) *
                      cityGoldMultiplier(*city, grid, capital, govDef);
    }
    efficiency += CONNECTION_EFFICIENCY * static_cast<float>(connectedCityCount(player, grid));
    // Civ ability: +N gold per active trade route, now N points of reach per route.
    const int32_t perRoute = civDef(player.civId()).modifiers.goldFromTradeRoute;
    if (perRoute > 0) {
        efficiency += ROUTE_ABILITY_POINT * static_cast<float>(perRoute) *
                      static_cast<float>(player.activeTradeRouteCount());
    }
    efficiency *= computeGovernmentModifiers(player.government()).goldMultiplier * allianceGoldMult;
    return std::clamp(efficiency, 0.0f, 1.0f);
}

EconomicBreakdown computeEconomicBreakdown(const aoc::game::Player& player,
                                           const aoc::map::HexGrid& grid, float allianceGoldMult) {
    EconomicBreakdown bd{};
    tallyUpkeepAndStock(player, bd);
    bd.incomeTradeRoutes = routeGoldEarned(player);
    if (const aoc::sim::MoneyLedger* ledger = player.moneyLedger(); ledger != nullptr) {
        const MoneyLedger::Civ& book = ledger->civs[static_cast<std::size_t>(player.id())];
        bd.incomeSeigniorage         = book.seigniorage;
        bd.incomeExternal            = book.externalIn;
    }
    if (moneyless(player)) {
        bd.totalIncome  = bd.incomeSeigniorage + bd.incomeTariffs + bd.incomeExternal;
        bd.totalExpense = bd.expenseUnits + bd.expenseBuildings;
        bd.netFlow      = -bd.totalExpense;
        return bd;
    }

    // The tax: the flow the people's money makes, at the rate, as far as the
    // state reaches; the goldAllocation share is what the treasury keeps, the
    // rest it spends straight back on luxuries and learning.
    const MonetaryStateComponent& money = player.monetary();
    bd.collectionEfficiency = collectionEfficiency(player, grid, allianceGoldMult);
    bd.taxBase = toGold(static_cast<float>(std::max<CurrencyAmount>(0, money.privateSpecie)) *
                        money.taxableMoneyShare());
    const float due = static_cast<float>(bd.taxBase) * money.taxRate * bd.collectionEfficiency;
    bd.incomeTax    = toGold(due * money.goldAllocation);

    bd.effectiveIncome = bd.incomeTax;
    bd.totalIncome = bd.incomeTax + bd.incomeSeigniorage + bd.incomeTariffs + bd.incomeExternal;
    bd.expenseScience =
        toGold(computePlayerScience(player, grid) * SCIENCE_FUNDING_COST * money.priceLevel);
    bd.totalExpense = bd.expenseUnits + bd.expenseBuildings + bd.expenseScience;
    bd.netFlow      = bd.effectiveIncome - bd.totalExpense;
    return bd;
}

CurrencyAmount processGoldIncome(aoc::game::Player& player, const aoc::map::HexGrid& grid,
                                 float allianceGoldMult) {
    const EconomicBreakdown bd = computeEconomicBreakdown(player, grid, allianceGoldMult);
    takeFromPrivate(player, bd.incomeTax);
    player.setIncomePerTurn(bd.totalIncome);
    return bd.totalIncome;
}

CurrencyAmount processUnitMaintenance(aoc::game::GameState& gameState,
                                      const aoc::map::HexGrid& grid, aoc::game::Player& player) {
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

    // Under Barter units are kept by the city's food and labour, not money.
    if (moneyless(player)) {
        return 0;
    }

    // Minimum garrison we never disband below.
    constexpr int32_t MIN_GARRISON = 2;
    // Turns of unpaid bills the soldiers put up with before one lot walks.
    constexpr int32_t ARREARS_GRACE_TURNS = 5;
    // Tax rate forced while in arrears (below the global ceiling).
    constexpr float ARREARS_TAX_RATE = 0.40f;

    // Bills by province: each unit is paid where it stands, so a garrison in
    // a foreign province pays that civ's people. Unowned land pays our own.
    std::map<PlayerId, CurrencyAmount> bills;
    int32_t paidUnits = 0;
    for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
        const int32_t cost = unit->typeDef().maintenanceGold();
        if (cost <= 0) {
            continue;
        }
        const PlayerId province =
            grid.isValid(unit->position()) ? grid.owner(grid.toIndex(unit->position())) : INVALID_PLAYER;
        bills[province] += static_cast<CurrencyAmount>(cost);
        ++paidUnits;
    }
    CurrencyAmount total  = 0;
    CurrencyAmount unpaid = 0;
    for (const std::pair<const PlayerId, CurrencyAmount>& bill : bills) {
        total += bill.second;
        unpaid += bill.second - payFromTreasury(gameState, player, bill.second, bill.first);
    }
    if (total > 0) {
        LOG_INFO("Player %u unit maintenance: %d units, %lld of %lld gold paid (treasury: %lld)",
                 static_cast<unsigned>(player.id()), paidUnits, static_cast<long long>(total - unpaid),
                 static_cast<long long>(total), static_cast<long long>(player.treasury()));
    }

    // Arrears: an unpaid bill is not borrowed money, it is soldiers unpaid.
    if (unpaid > 0) {
        ++player.monetary().consecutiveNegativeTurns;
    } else {
        player.monetary().consecutiveNegativeTurns = 0;
    }
    if (player.monetary().consecutiveNegativeTurns < ARREARS_GRACE_TURNS) {
        return unpaid;
    }
    if (player.monetary().taxRate < ARREARS_TAX_RATE) {
        setTaxRate(player.monetary(), ARREARS_TAX_RATE);
        LOG_WARN("Player %u [Maintenance.cpp:processUnitMaintenance] %d turns in arrears: "
                 "tax rate forced to %.2f",
                 static_cast<unsigned>(player.id()), player.monetary().consecutiveNegativeTurns,
                 static_cast<double>(ARREARS_TAX_RATE));
    }
    int32_t militaryCount = 0;
    for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
        if (isMilitary(unit->typeDef().unitClass)) {
            ++militaryCount;
        }
    }
    if (militaryCount <= MIN_GARRISON) {
        return unpaid;
    }
    // The most expensive military unit walks; failing that, any non-settler.
    aoc::game::Unit* disbandTarget = nullptr;
    int32_t worstCost              = 0;
    for (const bool militaryOnly : {true, false}) {
        for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
            if (unit->typeDef().unitClass == UnitClass::Settler) {
                continue;
            }
            if (militaryOnly && !isMilitary(unit->typeDef().unitClass)) {
                continue;
            }
            const int32_t cost = unit->typeDef().maintenanceGold();
            if (cost > worstCost) {
                worstCost     = cost;
                disbandTarget = unit.get();
            }
        }
        if (disbandTarget != nullptr) {
            break;
        }
    }
    if (disbandTarget != nullptr) {
        LOG_WARN("Player %u [Maintenance.cpp:processUnitMaintenance] %d turns in arrears "
                 "(%lld unpaid this turn): disbanded %s (cost %d gold/turn)",
                 static_cast<unsigned>(player.id()), player.monetary().consecutiveNegativeTurns,
                 static_cast<long long>(unpaid), disbandTarget->typeDef().name.data(), worstCost);
        player.removeUnit(disbandTarget);
        // The grace period starts over so one lot walks, not one a turn.
        player.monetary().consecutiveNegativeTurns = 0;
    }
    return unpaid;
}

CurrencyAmount processBuildingMaintenance(aoc::game::Player& player) {
    if (moneyless(player)) {
        return 0;
    }

    CurrencyAmount totalMaintenance = 0;
    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        if (city == nullptr || city->owner() != player.id()) {
            continue;
        }
        // Building maintenance from building definitions only; a district's
        // upkeep is embodied in the buildings inside it.
        for (const CityDistrictsComponent::PlacedDistrict& district : city->districts().districts) {
            for (BuildingId bid : district.buildings) {
                totalMaintenance += static_cast<CurrencyAmount>(buildingDef(bid).maintenanceCost);
            }
        }
        // Empire sprawl: 1 gold per city beyond the first.
        if (!city->isOriginalCapital()) {
            totalMaintenance += 1;
        }
    }
    if (totalMaintenance <= 0) {
        return 0;
    }

    const float priceMultiplier = priceLevelMaintenanceMultiplier(player.monetary().priceLevel);
    const CurrencyAmount adjustedMaintenance =
        static_cast<CurrencyAmount>(static_cast<float>(totalMaintenance) * priceMultiplier);
    // Paid to our own people; what the treasury lacks goes unpaid.
    const CurrencyAmount paid = payFromTreasury(player, adjustedMaintenance);
    LOG_INFO("Player %u building/city maintenance: %lld of %lld gold paid (treasury: %lld)",
             static_cast<unsigned>(player.id()), static_cast<long long>(paid),
             static_cast<long long>(adjustedMaintenance), static_cast<long long>(player.treasury()));
    return adjustedMaintenance - paid;
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
