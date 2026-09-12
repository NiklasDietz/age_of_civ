/**
 * @file test_local_prices.cpp
 * @brief Local prices (plan B5, 3.1): one consumption rule shared by needs
 *        and prices, a derived local price that rises with want and falls
 *        with stock inside [0.5, 2.5] of the market, a destination
 *        multiplier capped at 1.40, cargo chosen by the spread, and an
 *        estimate that prefers the city that lacks what we carry.
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
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

using aoc::PlayerId;
using aoc::sim::DistrictType;
using aoc::sim::TradeRouteType;
using aoc::sim::goods::CLOTHING;
using aoc::sim::goods::SILK;
using aoc::sim::goods::WHEAT;
using aoc::sim::goods::WINE;

namespace {

constexpr PlayerId P0{0};
constexpr PlayerId P1{1};
constexpr aoc::UnitTypeId TRADER{30};
constexpr aoc::BuildingId MARKET{6};
constexpr aoc::BuildingId BANK{20};
constexpr aoc::BuildingId STOCK_EXCHANGE{21};

[[nodiscard]] aoc::sim::Market marketAtBase() {
    aoc::sim::Market m;
    m.initialize();
    return m;
}

} // namespace

TEST_CASE("the consumption rule follows the population thresholds and is zero for the rest") {
    CHECK(aoc::sim::cityConsumptionNeed(WHEAT, 9) == 3);
    CHECK(aoc::sim::cityConsumptionNeed(CLOTHING, 9) == 2);
    CHECK(aoc::sim::cityConsumptionNeed(aoc::sim::goods::CONSUMER_GOODS, 3) == 0);
    CHECK(aoc::sim::cityConsumptionNeed(aoc::sim::goods::CONSUMER_GOODS, 4) == 1);
    CHECK(aoc::sim::cityConsumptionNeed(aoc::sim::goods::PROCESSED_FOOD, 8) == 0);
    CHECK(aoc::sim::cityConsumptionNeed(aoc::sim::goods::PROCESSED_FOOD, 12) == 2);
    CHECK(aoc::sim::cityConsumptionNeed(aoc::sim::goods::ADV_CONSUMER_GOODS, 20) == 2);
    CHECK(aoc::sim::cityConsumptionNeed(SILK, 30) == 0);
    CHECK(aoc::sim::cityConsumptionNeed(WHEAT, -5) == 0);
}

TEST_CASE("a local price rises with want and falls with stock, inside the band") {
    aoc::test::World w    = aoc::test::makeWorld(1);
    aoc::game::City& city = aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    const aoc::sim::Market market = marketAtBase();
    const int32_t base            = market.marketData(WHEAT).currentPrice;
    REQUIRE(base > 0);

    city.setPopulation(30); // needs 10 wheat a turn
    const int32_t starving = aoc::sim::localPrice(market, WHEAT, city);
    CHECK(starving > base);
    CHECK(starving <= static_cast<int32_t>(static_cast<float>(base) * aoc::sim::LOCAL_PRICE_MAX + 1.0f));

    city.stockpile().addGoods(WHEAT, 10); // exactly covered: the market price
    CHECK(aoc::sim::localPrice(market, WHEAT, city) == base);

    city.stockpile().addGoods(WHEAT, 500); // glutted: the floor
    const int32_t glutted = aoc::sim::localPrice(market, WHEAT, city);
    CHECK(glutted < base);
    CHECK(glutted >= static_cast<int32_t>(static_cast<float>(base) * aoc::sim::LOCAL_PRICE_MIN));

    // A good nobody consumes is at the market price when the city has none.
    CHECK(aoc::sim::localPrice(market, SILK, city) == market.marketData(SILK).currentPrice);
}

TEST_CASE("the destination multiplier adds up its commerce and caps at 1.40, the Harbor for Sea only") {
    aoc::test::World w    = aoc::test::makeWorld(1);
    aoc::game::City& city = aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    CHECK(aoc::sim::destinationSaleMultiplier(city, TradeRouteType::Land) == doctest::Approx(1.0f));
    city.districts().districts.push_back({DistrictType::Commercial, {7, 5}, {MARKET}});
    CHECK(aoc::sim::destinationSaleMultiplier(city, TradeRouteType::Land) == doctest::Approx(1.15f));
    city.districts().districts.push_back({DistrictType::Harbor, {6, 6}, {}});
    CHECK(aoc::sim::destinationSaleMultiplier(city, TradeRouteType::Land) == doctest::Approx(1.15f));
    CHECK(aoc::sim::destinationSaleMultiplier(city, TradeRouteType::Sea) == doctest::Approx(1.25f));
    city.districts().districts.front().buildings.push_back(BANK);
    city.districts().districts.front().buildings.push_back(STOCK_EXCHANGE);
    CHECK(aoc::sim::destinationSaleMultiplier(city, TradeRouteType::Sea) ==
          doctest::Approx(aoc::sim::DESTINATION_SALE_CAP));
}

TEST_CASE("the estimate prefers the destination that lacks what we carry, and values it by the spread") {
    aoc::test::World w      = aoc::test::makeWorld(2, 40, 24);
    aoc::game::City& home   = aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    aoc::game::City& hungry = aoc::test::addCityAt(w, P1, 15, 5, "Beta");
    aoc::game::City& fed    = aoc::test::addCityAt(w, P1, 25, 5, "Gamma");
    hungry.setPopulation(30);
    fed.setPopulation(30);
    home.stockpile().addGoods(WHEAT, 40);
    fed.stockpile().addGoods(WHEAT, 200);
    aoc::game::Unit& trader = aoc::test::addUnitAt(w, P0, TRADER, 5, 5);
    const aoc::sim::Market market = marketAtBase();

    const aoc::sim::TradeRouteEstimate toHungry =
        aoc::sim::estimateTradeRouteIncome(w.gameState, w.grid, market, trader, hungry);
    const aoc::sim::TradeRouteEstimate toFed =
        aoc::sim::estimateTradeRouteIncome(w.gameState, w.grid, market, trader, fed);
    CHECK(toHungry.estimatedGoldPerTrip > 0);
    CHECK(toHungry.estimatedGoldPerTrip > toFed.estimatedGoldPerTrip);
    // The preview is the sale of the cargo the route would load (twelve
    // wheat: half the surplus doubled for a city short of it, capped), by
    // the same function the delivery uses.
    const std::vector<aoc::sim::TradeCargo> cargo{{WHEAT, 12}};
    const float yield = aoc::sim::routeYieldMultiplier(w.gameState, nullptr, P0, P1, 10);
    CHECK(toHungry.estimatedGoldPerTrip ==
          aoc::sim::saleValueAt(w.gameState, market, cargo, hungry, TradeRouteType::Land, yield));
}

TEST_CASE("cargo is chosen by the spread: the good the destination is short of goes first") {
    aoc::test::World w    = aoc::test::makeWorld(2);
    aoc::game::City& home = aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    aoc::game::City& dest = aoc::test::addCityAt(w, P1, 12, 5, "Beta");
    dest.setPopulation(30);                 // wants wheat and clothing
    home.stockpile().addGoods(WHEAT, 20);   // both on offer
    home.stockpile().addGoods(WINE, 20);    // nobody consumes wine: no spread
    dest.stockpile().addGoods(WINE, 20);
    aoc::game::Unit& trader = aoc::test::addUnitAt(w, P0, TRADER, 5, 5);
    aoc::sim::Market market = marketAtBase();
    aoc::sim::DiplomacyManager diplomacy;
    diplomacy.initialize(2);
    diplomacy.meetPlayers(P0, P1, 1);
    w.gameState.player(P0)->monetary().system = aoc::sim::MonetarySystemType::CommodityMoney;
    REQUIRE(aoc::sim::establishTradeRoute(w.gameState, w.grid, market, &diplomacy, trader, dest) == aoc::ErrorCode::Ok);
    REQUIRE_FALSE(trader.trader().cargo.empty());
    CHECK(trader.trader().cargo.front().goodId == WHEAT);
    for (const aoc::sim::TradeCargo& c : trader.trader().cargo) {
        CHECK(c.goodId != WINE);
    }
}
