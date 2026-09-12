/**
 * @file test_money_conservation.cpp
 * @brief The money seam of the conserved ledger (plan B1, Phase 2.1): coin
 *        goods swept into bullion or private money at face value and booked
 *        as minted, a domestic tax drawing private money with the shortfall
 *        booked as unbacked, transfers moving money without booking, the
 *        external sector and losses booked, and the one-turn invariant
 *        agreeing with the books. Phases 2.2-2.4 grow this file.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/monetary/MoneyFlow.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/resource/EconomySimulation.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

using aoc::PlayerId;
using aoc::sim::MoneyFlow;
using aoc::sim::MoneyLedger;
using aoc::sim::goods::COPPER_COINS;
using aoc::sim::goods::GOLD_BARS;
using aoc::sim::goods::SILVER_COINS;

namespace {

constexpr PlayerId P0{0};
constexpr PlayerId P1{1};

/// Ten copper, two silver and one gold bar: face value 45.
constexpr int64_t MINTED_FACE = 10 * aoc::sim::COPPER_COIN_VALUE + 2 * aoc::sim::SILVER_COIN_VALUE +
                                1 * aoc::sim::GOLD_BAR_VALUE;

void mint(aoc::game::City& city) {
    city.stockpile().addGoods(COPPER_COINS, 10);
    city.stockpile().addGoods(SILVER_COINS, 2);
    city.stockpile().addGoods(GOLD_BARS, 1);
}

} // namespace

TEST_CASE("under Barter the sweep turns minted coin into bullion and books it as minted") {
    aoc::test::World w     = aoc::test::makeWorld(2);
    aoc::game::City& alpha = aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    aoc::test::addCityAt(w, P1, 14, 8, "Beta");
    mint(alpha);
    aoc::game::Player& p = *w.gameState.player(P0);
    REQUIRE(p.monetary().system == aoc::sim::MonetarySystemType::Barter);
    const int64_t before = aoc::sim::worldMoney(w.gameState);

    aoc::sim::EconomySimulation economy;
    economy.executeTurn(w.gameState, w.grid);

    CHECK(alpha.stockpile().getAmount(COPPER_COINS) == 0); // no coin good outlives the sweep
    CHECK(alpha.stockpile().getAmount(SILVER_COINS) == 0);
    CHECK(alpha.stockpile().getAmount(GOLD_BARS) == 0);
    CHECK(p.monetary().bullion == MINTED_FACE);
    CHECK(p.monetary().copperCoinReserves == 10);
    CHECK(p.monetary().silverCoinReserves == 2);
    CHECK(p.monetary().goldBarReserves == 1);
    CHECK(economy.moneyLedger().civs[0].minted == MINTED_FACE);
    CHECK(aoc::sim::worldMoney(w.gameState) == before + MINTED_FACE);
}

TEST_CASE("under coinage the sweep converts the bullion, pays the Mint its seigniorage, and keeps the metal count") {
    aoc::test::World w     = aoc::test::makeWorld(1);
    aoc::game::City& alpha = aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    aoc::game::Player& p   = *w.gameState.player(P0);
    p.monetary().system    = aoc::sim::MonetarySystemType::CommodityMoney;
    p.monetary().bullion   = 100;
    p.monetary().copperCoinReserves = 7; // minted earlier: the counters accumulate
    alpha.stockpile().addGoods(COPPER_COINS, 20); // face 20
    const aoc::CurrencyAmount treasuryBefore = p.treasury();

    aoc::sim::EconomySimulation economy;
    p.setMoneyLedger(&economy.moneyLedger()); // processTurn binds it; here we do
    economy.executeTurn(w.gameState, w.grid);

    const int64_t seigniorage = 20 * aoc::sim::SEIGNIORAGE_PCT / 100;
    CHECK(p.monetary().bullion == 0);
    CHECK(p.monetary().privateSpecie == 100 + 20 - seigniorage);
    CHECK(p.treasury() == treasuryBefore + seigniorage);
    CHECK(p.monetary().copperCoinReserves == 27);
    CHECK(economy.moneyLedger().civs[0].minted == 20);
}

TEST_CASE("a domestic tax draws private money and books what the people could not pay as unbacked") {
    aoc::test::World w   = aoc::test::makeWorld(1);
    aoc::game::Player& p = *w.gameState.player(P0);
    MoneyLedger ledger;
    p.setMoneyLedger(&ledger);
    p.monetary().privateSpecie = 30;
    const aoc::CurrencyAmount treasuryBefore = p.treasury();

    p.addGold(50, MoneyFlow::domestic(P0));
    CHECK(p.treasury() == treasuryBefore + 50);
    CHECK(p.monetary().privateSpecie == 0);
    CHECK(ledger.civs[0].unbackedIn == 20);

    p.addGold(-20, MoneyFlow::domestic(P0)); // spent back into the economy
    CHECK(p.treasury() == treasuryBefore + 30);
    CHECK(p.monetary().privateSpecie == 20);
    CHECK(ledger.civs[0].unbackedOut == 0);
    CHECK_FALSE(ledger.backed());
}

TEST_CASE("a transfer between treasuries books nothing; external, loss and unbacked flows are booked") {
    aoc::test::World w   = aoc::test::makeWorld(2);
    aoc::game::Player& a = *w.gameState.player(P0);
    aoc::game::Player& b = *w.gameState.player(P1);
    MoneyLedger ledger;
    a.setMoneyLedger(&ledger);
    b.setMoneyLedger(&ledger);
    a.setTreasury(100, MoneyFlow::external());
    CHECK(ledger.civs[0].externalIn == 100);
    const int64_t world = aoc::sim::worldMoney(w.gameState);

    a.addGold(-40, MoneyFlow::transfer(P1));
    b.addGold(40, MoneyFlow::transfer(P0));
    CHECK(aoc::sim::worldMoney(w.gameState) == world);
    CHECK(ledger.total().externalIn == 100);
    CHECK(ledger.backed());
    CHECK(ledger.expectedDelta() == 100);

    CHECK(a.spendGold(3, MoneyFlow::loss()));
    CHECK_FALSE(a.spendGold(1000, MoneyFlow::loss())); // cannot overdraw
    CHECK(ledger.civs[0].lost == 3);
    a.addGold(7, MoneyFlow::unbacked());
    a.addGold(-2, MoneyFlow::unbacked());
    CHECK(ledger.civs[0].unbackedIn == 7);
    CHECK(ledger.civs[0].unbackedOut == 2);
    CHECK_FALSE(ledger.backed());
    CHECK(ledger.expectedDelta() == 100 - 3 + 7 - 2);
    ledger.reset();
    CHECK(ledger.total().externalIn == 0);
}

TEST_CASE("the one-turn invariant agrees with the books over an economy step") {
    aoc::test::World w     = aoc::test::makeWorld(2);
    aoc::game::City& alpha = aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    aoc::test::addCityAt(w, P1, 14, 8, "Beta");
    mint(alpha);
    aoc::sim::EconomySimulation economy;
    for (const std::unique_ptr<aoc::game::Player>& player : w.gameState.players()) {
        player->setMoneyLedger(&economy.moneyLedger());
    }
    const int64_t before = aoc::sim::worldMoney(w.gameState);
    economy.executeTurn(w.gameState, w.grid);
    const int64_t after = aoc::sim::worldMoney(w.gameState);
    CHECK(after - before == economy.moneyLedger().expectedDelta());
    CHECK(aoc::sim::moneyConserved(before, after, economy.moneyLedger()));

    // One unbacked credit and the invariant no longer holds.
    w.gameState.player(P0)->addGold(5, MoneyFlow::unbacked());
    CHECK_FALSE(aoc::sim::moneyConserved(before, aoc::sim::worldMoney(w.gameState), economy.moneyLedger()));
}
