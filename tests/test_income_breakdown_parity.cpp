/**
 * @file test_income_breakdown_parity.cpp
 * @brief The treasury credit and the diagnostic breakdown are one function.
 *
 * computeEconomicBreakdown was a hand-kept mirror of processGoldIncome and had
 * drifted: Palace 5 against 10, raw against effective tile yields, and no
 * wonders, corruption, governor or government multiplier at all, so the CSV
 * and the HUD described an economy the treasury never saw. processGoldIncome
 * is now the breakdown plus addGold; these cases pin every rule the treasury
 * applies into the breakdown.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/simulation/city/CityScience.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/economy/Maintenance.hpp"
#include "aoc/simulation/government/Government.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/wonder/Wonder.hpp"

using aoc::CurrencyAmount;
using aoc::PlayerId;
using aoc::sim::DistrictType;
using aoc::sim::EconomicBreakdown;
using aoc::sim::GovernmentType;
using aoc::sim::GovernorType;

namespace {

constexpr PlayerId P0{0};
constexpr aoc::UnitTypeId TRADER{30};
constexpr aoc::UnitTypeId WARRIOR{0};
constexpr aoc::BuildingId MARKET{6};
constexpr aoc::sim::WonderId BIG_BEN{9};

/// The civ whose ability pays gold per active route.
[[nodiscard]] aoc::sim::CivId routeGoldCiv() {
    for (uint8_t i = 0; i < aoc::sim::CIV_COUNT; ++i) {
        const aoc::sim::CivId id = static_cast<aoc::sim::CivId>(i);
        if (aoc::sim::civDef(id).modifiers.goldFromTradeRoute > 0) {
            return id;
        }
    }
    return aoc::sim::CivId{0};
}

/// An empire that touches every income rule at once: a capital with a Market,
/// Big Ben, a Financier, a worked Trading Post and consumer goods; a colony
/// for distance corruption; coin in reserve for the money-supply tax; a
/// Merchant Republic for the government multiplier; a warrior for upkeep; two
/// active routes on a civ that pays per route.
struct Empire {
    aoc::test::World world    = aoc::test::makeWorld(1, 40, 24);
    aoc::game::Player& player = *world.gameState.players()[0];
    aoc::game::City* capital  = nullptr;
    aoc::game::City* colony   = nullptr;

    explicit Empire(aoc::hex::AxialCoord colonyAt = {30, 5}) {
        capital = &aoc::test::addCityAt(world, P0, 5, 5, "Capital");
        capital->setPopulation(8);
        capital->districts().districts.push_back({DistrictType::Commercial, {7, 5}, {MARKET}});
        capital->wonders().wonders.push_back(BIG_BEN);
        capital->governor().assignedGovernor = GovernorType::Financier;
        const aoc::hex::AxialCoord post{6, 5};
        world.grid.setImprovement(world.grid.toIndex(post), aoc::map::ImprovementType::TradingPost);
        capital->workedTiles().push_back(post);
        capital->stockpile().addGoods(aoc::sim::goods::CONSUMER_GOODS, 20);

        colony = &aoc::test::addCityAt(world, P0, colonyAt.q, colonyAt.r, "Colony");
        colony->setPopulation(5);

        player.monetary().system             = aoc::sim::MonetarySystemType::CommodityMoney;
        player.monetary().copperCoinReserves = 400;
        player.government().government       = GovernmentType::MerchantRepublic;
        player.setCivId(routeGoldCiv());
        aoc::test::addUnitAt(world, P0, WARRIOR, 5, 6);
        for (const int32_t q : {8, 9}) {
            aoc::test::addUnitAt(world, P0, TRADER, q, 5).trader().owner = P0;
        }
    }

    [[nodiscard]] EconomicBreakdown breakdown() const {
        return aoc::sim::computeEconomicBreakdown(player, world.grid);
    }

    CurrencyAmount collect() {
        return aoc::sim::processGoldIncome(player, world.grid);
    }
};

[[nodiscard]] CurrencyAmount channelSum(const EconomicBreakdown& bd) {
    return bd.incomeCapital + bd.incomeTax + bd.incomeIndustrial + bd.incomeTileGold +
           bd.incomeCommercial + bd.incomeGoodsEcon + bd.incomeMoneyTax;
}

} // namespace

TEST_CASE("processGoldIncome credits the breakdown's effective income and reports its total") {
    Empire e;
    const EconomicBreakdown bd = e.breakdown();
    // Every rule fires, so the parity below is not an equality of zeros.
    CHECK(bd.incomeCapital > 0);
    CHECK(bd.incomeTax > 0);
    CHECK(bd.incomeTileGold > 0);
    CHECK(bd.incomeCommercial > 0);
    CHECK(bd.incomeGoodsEcon > 0);
    CHECK(bd.incomeMoneyTax > 0);
    CHECK(bd.expenseUnits > 0);
    CHECK(bd.expenseScience > 0);
    CHECK(bd.effectiveIncome < bd.totalIncome); // the goldAllocation split

    const CurrencyAmount before   = e.player.treasury();
    const CurrencyAmount reported = e.collect();
    CHECK(e.player.treasury() - before == bd.effectiveIncome);
    CHECK(reported == bd.totalIncome);
    CHECK(e.player.incomePerTurn() == bd.totalIncome);
}

TEST_CASE("the channels sum to the totals, so the CSV reconciles") {
    Empire e;
    const EconomicBreakdown bd = e.breakdown();
    CHECK(channelSum(bd) == bd.totalIncome);
    CHECK(bd.totalExpense == bd.expenseUnits + bd.expenseBuildings + bd.expenseScience);
    CHECK(bd.netFlow == bd.effectiveIncome - bd.totalExpense);
}

TEST_CASE("the Palace pays 10, the number the treasury always received") {
    aoc::test::World w   = aoc::test::makeWorld(1);
    aoc::game::Player& p = *w.gameState.players()[0];
    p.monetary().system  = aoc::sim::MonetarySystemType::CommodityMoney;
    aoc::test::addCityAt(w, P0, 5, 5, "Capital");
    CHECK(aoc::sim::computeEconomicBreakdown(p, w.grid).incomeCapital == 10);
}

TEST_CASE("Big Ben adds its bonus and doubles the Market's gold") {
    Empire e;
    e.capital->governor().assignedGovernor = GovernorType::None;       // multipliers off,
    e.player.government().government       = GovernmentType::Chiefdom; // so the delta is exact
    const CurrencyAmount with              = e.breakdown().incomeCommercial;
    e.capital->wonders().wonders.clear();
    const CurrencyAmount without = e.breakdown().incomeCommercial;
    const float flat             = aoc::sim::wonderDef(BIG_BEN).effect.goldBonus;
    CHECK(with - without ==
          static_cast<CurrencyAmount>(flat) + aoc::sim::buildingDef(MARKET).goldBonus);
}

TEST_CASE("distance corruption discounts the colony, and the capital not at all") {
    Empire near{{5, 8}};
    Empire far{{30, 5}};
    CHECK(far.breakdown().incomeTax < near.breakdown().incomeTax);
    CHECK(far.breakdown().incomeCapital == near.breakdown().incomeCapital);
}

TEST_CASE("a Financier lifts the host city's gold, and the treasury sees it") {
    Empire e;
    const EconomicBreakdown with           = e.breakdown();
    e.capital->governor().assignedGovernor = GovernorType::None;
    const EconomicBreakdown without        = e.breakdown();
    CHECK(with.incomeTax > without.incomeTax);
    CHECK(with.totalIncome > without.totalIncome);
    const CurrencyAmount before = e.player.treasury();
    e.collect();
    CHECK(e.player.treasury() - before == without.effectiveIncome);
}

TEST_CASE("the government multiplier scales the whole income") {
    Empire e;
    const CurrencyAmount republic    = e.breakdown().totalIncome;
    e.player.government().government = GovernmentType::Chiefdom;
    const CurrencyAmount chiefdom    = e.breakdown().totalIncome;
    CHECK(republic > chiefdom);
}

TEST_CASE("the money-supply tax has its own channel instead of hiding in commercial gold") {
    Empire e;
    const EconomicBreakdown coined         = e.breakdown();
    e.player.monetary().copperCoinReserves = 0;
    const EconomicBreakdown bare           = e.breakdown();
    CHECK(coined.incomeMoneyTax > 0);
    CHECK(bare.incomeMoneyTax == 0);
    CHECK(coined.incomeCommercial == bare.incomeCommercial);
    CHECK(coined.totalIncome - bare.totalIncome == coined.incomeMoneyTax);
}

TEST_CASE("route gold is reported beside the income, not counted in it") {
    Empire e;
    const EconomicBreakdown quiet = e.breakdown();
    CHECK(quiet.incomeTradeRoutes == 0);
    for (const std::unique_ptr<aoc::game::Unit>& unit : e.player.units()) {
        if (unit->typeDef().unitClass == aoc::sim::UnitClass::Trader) {
            unit->trader().goldEarnedThisTurn = 37;
            break;
        }
    }
    const EconomicBreakdown landed = e.breakdown();
    CHECK(landed.incomeTradeRoutes == 37);
    CHECK(landed.totalIncome == quiet.totalIncome);
    const CurrencyAmount before = e.player.treasury();
    e.collect();
    CHECK(e.player.treasury() - before == landed.effectiveIncome);
}

TEST_CASE("science funding is the breakdown's third expense") {
    Empire e;
    const EconomicBreakdown bd = e.breakdown();
    const float science        = aoc::sim::computePlayerScience(e.player, e.world.grid);
    CHECK(science > 0.0f);
    CHECK(bd.expenseScience ==
          static_cast<CurrencyAmount>(science * aoc::sim::SCIENCE_FUNDING_COST));
}

TEST_CASE("a moneyless Barter civ earns nothing, funds nothing, and still owes upkeep") {
    Empire e;
    e.player.monetary().system             = aoc::sim::MonetarySystemType::Barter;
    e.player.monetary().copperCoinReserves = 0;
    const EconomicBreakdown bd             = e.breakdown();
    CHECK(bd.totalIncome == 0);
    CHECK(bd.effectiveIncome == 0);
    CHECK(bd.expenseScience == 0);
    CHECK(bd.expenseUnits > 0);
    CHECK(bd.expenseBuildings == 1); // the colony's sprawl; a Market costs nothing
    CHECK(bd.goodsStockpiled == 20);
    CHECK(bd.netFlow == -bd.totalExpense);
    const CurrencyAmount before = e.player.treasury();
    CHECK(e.collect() == 0);
    CHECK(e.player.treasury() == before);
}
