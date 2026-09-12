/**
 * @file test_income_breakdown_parity.cpp
 * @brief The treasury's tax and the diagnostic breakdown are one function.
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

/// An empire that touches every collection rule at once: a capital with a
/// Market, Big Ben, a worked Trading Post and consumer goods; a colony for
/// distance corruption; private money for the tax; a warrior for upkeep; two
/// active routes on a civ whose ability pays per route. The Financier and the
/// Merchant Republic are set by the cases that test them: with both, the
/// reach hits its cap of 1 and comparisons would read 1 == 1.
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
        const aoc::hex::AxialCoord post{6, 5};
        world.grid.setImprovement(world.grid.toIndex(post), aoc::map::ImprovementType::TradingPost);
        capital->workedTiles().push_back(post);
        capital->stockpile().addGoods(aoc::sim::goods::CONSUMER_GOODS, 20);

        colony = &aoc::test::addCityAt(world, P0, colonyAt.q, colonyAt.r, "Colony");
        colony->setPopulation(5);

        player.monetary().system        = aoc::sim::MonetarySystemType::CommodityMoney;
        player.monetary().privateSpecie = 1000;
        player.government().government  = GovernmentType::Chiefdom;
        player.setCivId(routeGoldCiv());
        aoc::test::addUnitAt(world, P0, WARRIOR, 5, 6);
        for (const int32_t q : {8, 9}) {
            aoc::test::addUnitAt(world, P0, TRADER, q, 5).trader().owner = P0;
        }
    }

    [[nodiscard]] EconomicBreakdown breakdown() const {
        return aoc::sim::computeEconomicBreakdown(player, world.grid);
    }

    [[nodiscard]] float efficiency() const {
        return aoc::sim::collectionEfficiency(player, world.grid);
    }

    CurrencyAmount collect() {
        return aoc::sim::processGoldIncome(player, world.grid);
    }
};

[[nodiscard]] CurrencyAmount channelSum(const EconomicBreakdown& bd) {
    return bd.incomeTax + bd.incomeSeigniorage + bd.incomeTariffs + bd.incomeExternal;
}

} // namespace

TEST_CASE("processGoldIncome draws the breakdown's tax out of private money and reports the total") {
    Empire e;
    const EconomicBreakdown bd = e.breakdown();
    CHECK(bd.taxBase > 0);
    CHECK(bd.collectionEfficiency > aoc::sim::BASE_COLLECTION_EFFICIENCY);
    CHECK(bd.incomeTax > 0);
    CHECK(bd.effectiveIncome == bd.incomeTax);
    CHECK(bd.expenseUnits > 0);
    CHECK(bd.expenseScience > 0);

    const CurrencyAmount treasuryBefore = e.player.treasury();
    const CurrencyAmount privateBefore  = e.player.monetary().privateSpecie;
    const CurrencyAmount reported       = e.collect();
    CHECK(e.player.treasury() - treasuryBefore == bd.incomeTax);
    CHECK(privateBefore - e.player.monetary().privateSpecie == bd.incomeTax); // the money moved, nothing was made
    CHECK(reported == bd.totalIncome);
    CHECK(e.player.incomePerTurn() == bd.totalIncome);
}

TEST_CASE("the tax is the taxable flow at the rate, as far as the state reaches, kept by the allocation") {
    Empire e;
    const EconomicBreakdown bd = e.breakdown();
    const aoc::sim::MonetaryStateComponent& m = e.player.monetary();
    const CurrencyAmount base =
        static_cast<CurrencyAmount>(static_cast<float>(m.privateSpecie) * m.taxableMoneyShare());
    CHECK(bd.taxBase == base);
    const float due = static_cast<float>(base) * m.taxRate * bd.collectionEfficiency;
    CHECK(bd.incomeTax == static_cast<CurrencyAmount>(due * m.goldAllocation));
    CHECK(bd.incomeTax < base); // never the whole stock in one turn
}

TEST_CASE("the channels sum to the totals, so the CSV reconciles") {
    Empire e;
    const EconomicBreakdown bd = e.breakdown();
    CHECK(channelSum(bd) == bd.totalIncome);
    CHECK(bd.totalExpense == bd.expenseUnits + bd.expenseBuildings + bd.expenseScience);
    CHECK(bd.netFlow == bd.effectiveIncome - bd.totalExpense);
}

TEST_CASE("no money, no tax: a rich commerce reaches nothing when the people hold nothing") {
    Empire e;
    e.player.monetary().privateSpecie = 0;
    const EconomicBreakdown bd        = e.breakdown();
    CHECK(bd.taxBase == 0);
    CHECK(bd.incomeTax == 0);
    CHECK(bd.collectionEfficiency > aoc::sim::BASE_COLLECTION_EFFICIENCY); // the reach is there, the money is not
    const CurrencyAmount before = e.player.treasury();
    CHECK(e.collect() == 0);
    CHECK(e.player.treasury() == before);
}

TEST_CASE("a Market raises the collection, not the base") {
    Empire e;
    const EconomicBreakdown with = e.breakdown();
    for (aoc::sim::CityDistrictsComponent::PlacedDistrict& d : e.capital->districts().districts) {
        if (d.type == DistrictType::Commercial) {
            d.buildings.clear(); // the Market
        }
    }
    const EconomicBreakdown without = e.breakdown();
    CHECK(aoc::sim::buildingCollectionBonus(MARKET) == doctest::Approx(0.08f));
    CHECK(with.collectionEfficiency > without.collectionEfficiency);
    CHECK(with.taxBase == without.taxBase);
    CHECK(with.incomeTax > without.incomeTax);
}

TEST_CASE("Big Ben adds its wonder gold and doubles the market buildings' reach") {
    Empire e;
    const float with = e.efficiency();
    e.capital->wonders().wonders.clear();
    const float without = e.efficiency();
    CHECK(with > without);
}

TEST_CASE("the Palace is the capital's reach on top of the base") {
    aoc::test::World w   = aoc::test::makeWorld(1);
    aoc::game::Player& p = *w.gameState.players()[0];
    p.monetary().system  = aoc::sim::MonetarySystemType::CommodityMoney;
    aoc::test::addCityAt(w, P0, 5, 5, "Capital");
    const float govMult = aoc::sim::computeGovernmentModifiers(p.government()).goldMultiplier;
    CHECK(aoc::sim::collectionEfficiency(p, w.grid) ==
          doctest::Approx((aoc::sim::BASE_COLLECTION_EFFICIENCY + 0.10f) * govMult));
}

TEST_CASE("distance corruption discounts the colony's commerce, the capital's not at all") {
    Empire near{{5, 8}};
    Empire far{{30, 5}};
    for (Empire* e : {&near, &far}) {
        e->player.government().government = GovernmentType::MerchantRepublic; // has distance corruption
        e->capital->wonders().wonders.clear();                                // and stays under the cap
    }
    near.colony->districts().districts.push_back({DistrictType::Commercial, {6, 9}, {MARKET}});
    far.colony->districts().districts.push_back({DistrictType::Commercial, {31, 6}, {MARKET}});
    CHECK(far.efficiency() < near.efficiency());
    CHECK(far.efficiency() < 1.0f);
}

TEST_CASE("a Financier lifts the host city's reach, and the treasury sees it") {
    Empire e;
    e.capital->governor().assignedGovernor = GovernorType::Financier;
    const EconomicBreakdown with           = e.breakdown();
    e.capital->governor().assignedGovernor = GovernorType::None;
    const EconomicBreakdown without        = e.breakdown();
    CHECK(with.collectionEfficiency > without.collectionEfficiency);
    CHECK(with.incomeTax > without.incomeTax);
    const CurrencyAmount before = e.player.treasury();
    e.collect();
    CHECK(e.player.treasury() - before == without.incomeTax);
}

TEST_CASE("the government and an economic alliance multiply the reach") {
    Empire e;
    const float chiefdom             = e.efficiency();
    e.player.government().government = GovernmentType::MerchantRepublic;
    const float republic             = e.efficiency();
    e.player.government().government = GovernmentType::Chiefdom;
    CHECK(republic > chiefdom);
    CHECK(aoc::sim::collectionEfficiency(e.player, e.world.grid, 1.25f) > chiefdom);
    CHECK(aoc::sim::collectionEfficiency(e.player, e.world.grid, 100.0f) <= 1.0f); // capped
}

TEST_CASE("the customs on landed trade coin are counted in the income, moved by the Trader system") {
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
    CHECK(landed.totalIncome == quiet.totalIncome + 37);
    CHECK(landed.effectiveIncome == quiet.effectiveIncome); // processGoldIncome moves only the tax
}

TEST_CASE("seigniorage and the external sector are reported from the turn's ledger") {
    Empire e;
    aoc::sim::MoneyLedger ledger;
    e.player.setMoneyLedger(&ledger);
    const EconomicBreakdown quiet = e.breakdown();
    ledger.civs[0].seigniorage    = 5;
    ledger.civs[0].externalIn     = 7;
    const EconomicBreakdown paid  = e.breakdown();
    CHECK(paid.incomeSeigniorage == 5);
    CHECK(paid.incomeExternal == 7);
    CHECK(paid.incomeTariffs == 0); // Phase 3.3
    CHECK(paid.totalIncome == quiet.totalIncome + 12);
    CHECK(paid.effectiveIncome == quiet.effectiveIncome); // only the tax is moved here
}

TEST_CASE("science funding and upkeep are nominal at the price level") {
    Empire e;
    const float science = aoc::sim::computePlayerScience(e.player, e.world.grid);
    CHECK(science > 0.0f);
    e.player.monetary().priceLevel = 1.0f;
    const EconomicBreakdown par    = e.breakdown();
    CHECK(par.expenseScience == static_cast<CurrencyAmount>(science * aoc::sim::SCIENCE_FUNDING_COST));
    for (const int32_t q : {10, 11, 12, 13}) {
        aoc::test::addUnitAt(e.world, P0, WARRIOR, q, 6); // five warriors: a bill big enough to scale
    }
    const EconomicBreakdown army   = e.breakdown();
    e.player.monetary().priceLevel = 4.0f;
    const EconomicBreakdown dear   = e.breakdown();
    CHECK(dear.expenseScience == static_cast<CurrencyAmount>(science * aoc::sim::SCIENCE_FUNDING_COST * 4.0f));
    CHECK(dear.expenseUnits > army.expenseUnits);
    CHECK(dear.expenseUnits > par.expenseUnits);
}

TEST_CASE("a moneyless Barter civ earns nothing, funds nothing, and still owes upkeep") {
    Empire e;
    e.player.monetary().system = aoc::sim::MonetarySystemType::Barter;
    const EconomicBreakdown bd = e.breakdown();
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
