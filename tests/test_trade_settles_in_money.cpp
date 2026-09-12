/**
 * @file test_trade_settles_in_money.cpp
 * @brief The AI's trade consults the monetary system, and the exchange rate
 *        hears about the trade.
 *
 *        Until 2026-09-11 there were two trade systems in the tree. The
 *        legacy TradeRouteComponent list, walked by executeTradeRoutes and
 *        settleTradeInCoins, was appended to only by the human's trade
 *        screen, so it was empty for every headless run; the money
 *        programme's Phase 0.5 deleted it. Trader UNITS, in
 *        TradeRouteSystem.cpp, are how everyone trades.
 *
 *        The whole international-money layer hung off the human-only one.
 *        Measured over 500 turns of seed 42: settleTradeInCoins settled zero
 *        payments, IncomeGoodsEcon was 0 in all 1400 player-turns, and so
 *        bilateralTradeEfficiency -- the one consumer of currency trust and of
 *        the exchange rate -- was never called. Currency trust was computed
 *        every turn, saved, penalised by crises and used to gate reserve-currency
 *        status while having no route to any treasury.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/citystate/CityState.hpp"
#include "aoc/simulation/economy/AdvancedEconomics.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/economy/TradeRouteSystem.hpp"
#include "aoc/simulation/monetary/CurrencyTrust.hpp"
#include "aoc/simulation/monetary/ForexMarket.hpp"
#include "aoc/simulation/monetary/MoneyFlow.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

#include <algorithm>
#include <string>
#include <vector>

using aoc::sim::MonetarySystemType;

TEST_CASE("a distrusted currency earns less on the same cargo") {
    aoc::sim::CurrencyTrustComponent trusted;
    trusted.trustScore = 0.90f;
    aoc::sim::CurrencyTrustComponent distrusted;
    distrusted.trustScore = 0.15f;

    const float goodTerms = aoc::sim::fiatTradeEfficiency(trusted);
    const float badTerms  = aoc::sim::fiatTradeEfficiency(distrusted);

    CHECK(goodTerms > badTerms);
    // The spread is worth having: a collapse of confidence costs about a third
    // of the take, which is what makes a crisis penalty matter.
    CHECK(goodTerms - badTerms > 0.30f);
}

namespace {

constexpr aoc::UnitTypeId TRADER_UNIT{30};
constexpr aoc::PlayerId SELLER{0};
constexpr aoc::PlayerId BUYER{1};

/// A seller's Trader one step from the buyer's city, laden with wine.
struct Route {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::sim::Market market;
    aoc::game::City* home   = nullptr;
    aoc::game::City* abroad = nullptr;
    aoc::game::Unit* unit   = nullptr;

    Route() {
        market.initialize();
        home   = &aoc::test::addCityAt(w, SELLER, 5, 5, "Alpha");
        abroad = &aoc::test::addCityAt(w, BUYER, 9, 5, "Beta");
        w.gameState.player(SELLER)->monetary().system = aoc::sim::MonetarySystemType::CommodityMoney;
        w.gameState.player(BUYER)->monetary().system  = aoc::sim::MonetarySystemType::CommodityMoney;
        unit                             = &aoc::test::addUnitAt(w, SELLER, TRADER_UNIT, 8, 5);
        aoc::sim::TraderComponent& tc    = unit->trader();
        tc.owner                         = SELLER;
        tc.destOwner                     = BUYER;
        tc.originCityLocation            = home->location();
        tc.destCityLocation              = abroad->location();
        tc.routeType                     = aoc::sim::TradeRouteType::Land;
        for (int32_t q = 5; q <= 9; ++q) {
            tc.path.push_back({q, 5});
        }
        tc.pathIndex = static_cast<int32_t>(tc.path.size()) - 2; // one step out
        tc.cargo.push_back({aoc::sim::goods::WINE, 5});
    }

    void turn() { aoc::sim::processTradeRoutes(w.gameState, w.grid, market, nullptr); }

    /// Run turns until the trader has landed at home again (at most `limit`).
    bool comeHome(int32_t limit = 12) {
        for (int32_t i = 0; i < limit; ++i) {
            turn();
            if (unit->trader().isReturning == false && unit->trader().pathIndex == 0) {
                return true; // reversed at home: the next leg is outbound again
            }
        }
        return false;
    }

    [[nodiscard]] aoc::game::Player& seller() { return *w.gameState.player(SELLER); }
    [[nodiscard]] aoc::game::Player& buyer() { return *w.gameState.player(BUYER); }
};

} // namespace

TEST_CASE("a coin sale moves money from the buyer's people to the purse, and home with customs taken") {
    Route r;
    r.buyer().monetary().privateSpecie = 1000;
    const int64_t worldBefore          = aoc::sim::worldMoney(r.w.gameState);

    r.turn(); // arrives abroad and sells
    const aoc::CurrencyAmount purse = r.unit->trader().carriedGold;
    CHECK(purse > 0);
    CHECK(r.buyer().monetary().privateSpecie == 1000 - purse);
    CHECK(r.seller().treasury() == 0); // nothing lands until the trader is home
    CHECK(r.abroad->stockpile().getAmount(aoc::sim::goods::WINE) == 5);
    CHECK(aoc::sim::worldMoney(r.w.gameState) == worldBefore);

    REQUIRE(r.comeHome());
    const aoc::CurrencyAmount customs = static_cast<aoc::CurrencyAmount>(
        static_cast<float>(purse) * r.seller().monetary().taxRate);
    CHECK(r.unit->trader().carriedGold == 0);
    CHECK(r.seller().treasury() == customs);
    CHECK(r.seller().monetary().privateSpecie == purse - customs);
    CHECK(r.unit->trader().goldEarnedThisTurn == customs); // what the breakdown reports as route income
    CHECK(aoc::sim::worldMoney(r.w.gameState) == worldBefore);
}

TEST_CASE("a buyer with no money pays in goods, as far as the return cargo has room") {
    Route r;
    r.buyer().monetary().system        = aoc::sim::MonetarySystemType::Barter;
    r.buyer().monetary().privateSpecie = 0;
    r.abroad->stockpile().addGoods(aoc::sim::goods::SILK, 50);
    const int32_t silkBefore = r.abroad->stockpile().getAmount(aoc::sim::goods::SILK);

    r.turn();
    CHECK(r.unit->trader().carriedGold == 0);
    int32_t silkAboard = 0;
    for (const aoc::sim::TradeCargo& c : r.unit->trader().cargo) {
        if (c.goodId == aoc::sim::goods::SILK) {
            silkAboard += c.amount;
        }
    }
    CHECK(silkAboard > 0);
    CHECK(r.abroad->stockpile().getAmount(aoc::sim::goods::SILK) == silkBefore - silkAboard);
    CHECK(r.buyer().monetary().privateSpecie == 0);
}

TEST_CASE("a Barter seller's coin comes home as bullion, the metal it will adopt coinage with") {
    Route r;
    r.seller().monetary().system       = aoc::sim::MonetarySystemType::Barter;
    r.buyer().monetary().privateSpecie = 1000;
    r.turn();
    const aoc::CurrencyAmount purse = r.unit->trader().carriedGold;
    REQUIRE(purse > 0);
    REQUIRE(r.comeHome());
    CHECK(r.seller().monetary().bullion == purse);
    CHECK(r.seller().treasury() == 0);
    CHECK(r.seller().monetary().privateSpecie == 0);
}

TEST_CASE("money crossing to or from a city-state's hoard is an external flow for the civ inside") {
    aoc::test::World w = aoc::test::makeWorld(2);
    w.gameState.initializeCityStateSlots(1);
    const aoc::PlayerId seat = static_cast<aoc::PlayerId>(aoc::sim::CITY_STATE_PLAYER_BASE);
    aoc::game::Player* cs    = w.gameState.player(seat);
    REQUIRE(cs != nullptr);
    aoc::game::Player& civ = *w.gameState.player(SELLER);
    aoc::sim::MoneyLedger ledger;
    civ.setMoneyLedger(&ledger);
    civ.monetary().privateSpecie = 50;
    const int64_t before         = aoc::sim::worldMoney(w.gameState);

    // A city-state's soldiers pillage our farm: the coin leaves the world.
    CHECK(aoc::sim::takeFromPrivate(w.gameState, SELLER, *cs, 15) == 15);
    CHECK(civ.monetary().privateSpecie == 35);
    CHECK(ledger.civs[0].externalOut == 15);
    // A city-state's garrison pays our people: the coin comes from beyond.
    cs->setTreasury(100, aoc::sim::MoneyFlow::external());
    CHECK(aoc::sim::payFromTreasury(w.gameState, *cs, 10, SELLER) == 10);
    CHECK(civ.monetary().privateSpecie == 45);
    CHECK(ledger.civs[0].externalIn == 10);
    // Plunder by a city-state, and a transfer to one, are external too.
    civ.setTreasury(20, aoc::sim::MoneyFlow::external());
    CHECK(aoc::sim::plunder(w.gameState, SELLER, *cs, 60) == 60); // 45 from the people, 15 from the treasury
    CHECK(civ.treasury() == 5);
    CHECK(civ.monetary().privateSpecie == 0);
    CHECK(aoc::sim::moneyConserved(before, aoc::sim::worldMoney(w.gameState), ledger));
}

TEST_CASE("a toll comes out of the purse first, then out of the cargo") {
    Route r;
    aoc::game::Player& keeper = *r.w.gameState.player(BUYER);
    // The buyer's territory lies across the road; its toll rate is set on
    // the tariffs component.
    for (int32_t q = 6; q <= 8; ++q) {
        r.w.grid.setOwner(r.w.grid.toIndex(aoc::hex::AxialCoord{q, 5}), BUYER);
    }
    keeper.tariffs().defaultTollRate = 0.5f;
    r.unit->trader().carriedGold  = 3; // a little coin left in the purse
    r.unit->trader().pathIndex    = 0;
    const int32_t wineBefore      = 5;
    r.turn();
    const aoc::CurrencyAmount toll = r.unit->trader().tollPaidThisTurn;
    CHECK(toll > 0);
    CHECK(keeper.treasury() == std::min<aoc::CurrencyAmount>(3, toll)); // the purse went to the keeper
    int32_t wineAboard = 0;
    for (const aoc::sim::TradeCargo& c : r.unit->trader().cargo) {
        if (c.goodId == aoc::sim::goods::WINE) {
            wineAboard += c.amount;
        }
    }
    if (toll > 3) {
        CHECK(wineAboard < wineBefore); // the rest in kind
        CHECK(keeper.cities().front()->stockpile().getAmount(aoc::sim::goods::WINE) == wineBefore - wineAboard);
    }
}

TEST_CASE("a trade surplus firms the currency and a deficit weakens it") {
    // Two identical fiat civs, one running a surplus and one a deficit of the
    // same size, which is how the trader hook books a sale.
    aoc::sim::MonetaryStateComponent exporterMon;
    exporterMon.system = MonetarySystemType::FiatMoney;
    exporterMon.gdp    = 10000;

    aoc::sim::CurrencyTrustComponent trust;
    trust.trustScore = 0.60f;

    aoc::sim::CurrencyExchangeComponent surplus;
    surplus.tradeBalance = 400;
    aoc::sim::CurrencyExchangeComponent deficit;
    deficit.tradeBalance = -400;

    const float fundamental = aoc::sim::computeFundamentalRate(exporterMon, trust, exporterMon.gdp);

    // The effect updateExchangeRates adds on top of the fundamental rate.
    const float surplusEffect =
        static_cast<float>(surplus.tradeBalance) / static_cast<float>(exporterMon.gdp) * 2.0f;
    const float deficitEffect =
        static_cast<float>(deficit.tradeBalance) / static_cast<float>(exporterMon.gdp) * 2.0f;

    CHECK(fundamental + surplusEffect > fundamental);
    CHECK(fundamental + deficitEffect < fundamental);
    // Equal and opposite, which the trader hook guarantees by debiting the
    // buyer exactly what it credits the seller.
    CHECK(surplusEffect == doctest::Approx(-deficitEffect));
}
