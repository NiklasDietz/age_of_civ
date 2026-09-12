/**
 * @file test_route_margin.cpp
 * @brief Route pricing (plan 3.2): the yield multiplier's distance decay,
 *        relation and money terms; the Sea cargo cap without a Harbor; and
 *        the preview equalling the realized purse on an unchanged world.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/economy/TradeRouteSystem.hpp"
#include "aoc/simulation/monetary/CurrencyTrust.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

using aoc::PlayerId;
using aoc::sim::DistrictType;
using aoc::sim::TradeRouteType;

namespace {

constexpr PlayerId P0{0};
constexpr PlayerId P1{1};
constexpr aoc::UnitTypeId TRADER{30};

} // namespace

TEST_CASE("the yield decays with distance to a floor of a half, and that is all a domestic sale sees") {
    aoc::test::World w = aoc::test::makeWorld(2);
    CHECK(aoc::sim::routeYieldMultiplier(w.gameState, nullptr, P0, P0, 0) == doctest::Approx(1.0f));
    CHECK(aoc::sim::routeYieldMultiplier(w.gameState, nullptr, P0, P0, 12) == doctest::Approx(1.0f - 0.0167f * 12.0f));
    CHECK(aoc::sim::routeYieldMultiplier(w.gameState, nullptr, P0, P0, 30) == doctest::Approx(0.5f).epsilon(0.01));
    CHECK(aoc::sim::routeYieldMultiplier(w.gameState, nullptr, P0, P0, 100) == doctest::Approx(0.5f));
    // A city-state buyer: distance alone too.
    const PlayerId seat = static_cast<PlayerId>(aoc::sim::CITY_STATE_PLAYER_BASE);
    CHECK(aoc::sim::routeYieldMultiplier(w.gameState, nullptr, P0, seat, 0) == doctest::Approx(1.0f));
}

TEST_CASE("between two civs the relation and the money multiply the yield") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::sim::DiplomacyManager d;
    d.initialize(2);
    d.meetPlayers(P0, P1, 1);
    const float money = aoc::sim::bilateralTradeEfficiency(w.gameState, P0, P1);
    CHECK(money > 0.0f);
    CHECK(aoc::sim::routeYieldMultiplier(w.gameState, &d, P0, P1, 0) == doctest::Approx(money));
    d.declareWar(P0, P1);
    CHECK(aoc::sim::routeYieldMultiplier(w.gameState, &d, P0, P1, 0) == doctest::Approx(0.20f * money));
    // A fresh relation with standing terms: open borders and an economic
    // alliance add to the neutral stance.
    aoc::sim::DiplomacyManager friendly;
    friendly.initialize(2);
    friendly.meetPlayers(P0, P1, 1);
    friendly.relation(P0, P1).hasOpenBorders      = true;
    friendly.relation(P0, P1).hasEconomicAlliance = true;
    CHECK(aoc::sim::routeYieldMultiplier(w.gameState, &friendly, P0, P1, 0) ==
          doctest::Approx(1.25f * money));
    // Without a diplomacy manager only the money term applies.
    CHECK(aoc::sim::routeYieldMultiplier(w.gameState, nullptr, P0, P1, 0) == doctest::Approx(money));
}

TEST_CASE("a Sea leg carries four slots at most into a city without a Harbor") {
    aoc::test::World w    = aoc::test::makeWorld(1);
    aoc::game::City& city = aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    aoc::sim::TraderComponent sea{};
    sea.routeType = TradeRouteType::Sea;
    const int32_t full = sea.effectiveCargoSlots(aoc::sim::MonetarySystemType::FiatMoney, false);
    REQUIRE(full > aoc::sim::SEA_SLOTS_WITHOUT_HARBOR);
    CHECK(aoc::sim::legCargoSlots(sea, aoc::sim::MonetarySystemType::FiatMoney, false, 1.0f, city) ==
          aoc::sim::SEA_SLOTS_WITHOUT_HARBOR);
    city.districts().districts.push_back({DistrictType::Harbor, {6, 5}, {}});
    CHECK(aoc::sim::legCargoSlots(sea, aoc::sim::MonetarySystemType::FiatMoney, false, 1.0f, city) == full);
    // Land legs are not capped, and the industrial multiplier widens them.
    aoc::sim::TraderComponent land{};
    land.routeType = TradeRouteType::Land;
    CHECK(aoc::sim::legCargoSlots(land, aoc::sim::MonetarySystemType::FiatMoney, true, 1.0f, city) ==
          land.effectiveCargoSlots(aoc::sim::MonetarySystemType::FiatMoney, true));
    CHECK(aoc::sim::legCargoSlots(land, aoc::sim::MonetarySystemType::FiatMoney, true, 2.0f, city) ==
          2 * land.effectiveCargoSlots(aoc::sim::MonetarySystemType::FiatMoney, true));
}

TEST_CASE("the preview equals the purse the outbound leg brings back when nothing changes in between") {
    aoc::test::World w      = aoc::test::makeWorld(2, 40, 24);
    aoc::game::City& home   = aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    aoc::game::City& abroad = aoc::test::addCityAt(w, P1, 11, 5, "Beta");
    abroad.setPopulation(20);
    home.stockpile().addGoods(aoc::sim::goods::WHEAT, 40);
    home.stockpile().addGoods(aoc::sim::goods::CLOTHING, 30);
    aoc::game::Player& seller = *w.gameState.player(P0);
    aoc::game::Player& buyer  = *w.gameState.player(P1);
    seller.monetary().system  = aoc::sim::MonetarySystemType::CommodityMoney;
    buyer.monetary().system   = aoc::sim::MonetarySystemType::CommodityMoney;
    buyer.monetary().privateSpecie = 100000; // can pay any price
    aoc::sim::Market market;
    market.initialize();
    aoc::sim::DiplomacyManager d;
    d.initialize(2);
    d.meetPlayers(P0, P1, 1);
    aoc::game::Unit& unit = aoc::test::addUnitAt(w, P0, TRADER, 5, 5);

    const aoc::sim::TradeRouteEstimate preview =
        aoc::sim::estimateTradeRouteIncome(w.gameState, w.grid, market, unit, abroad, &d);
    REQUIRE(preview.estimatedGoldPerTrip > 0);
    REQUIRE(aoc::sim::establishTradeRoute(w.gameState, w.grid, market, &d, unit, abroad) == aoc::ErrorCode::Ok);
    for (int32_t turn = 0; turn < 20 && !unit.trader().isReturning; ++turn) {
        aoc::sim::processTradeRoutes(w.gameState, w.grid, market, &d);
    }
    REQUIRE(unit.trader().isReturning); // delivered, purse filled, turned for home
    CHECK(unit.trader().carriedGold == preview.estimatedGoldPerTrip);
}
