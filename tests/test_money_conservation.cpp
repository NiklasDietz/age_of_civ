/**
 * @file test_money_conservation.cpp
 * @brief The money seam of the conserved ledger (plan B1, Phase 2.1): a
 *        domestic tax drawing private money with the shortfall booked as
 *        unbacked, transfers moving money without booking, the external
 *        sector and losses booked, and the one-turn invariant agreeing with
 *        the books. Spend-back (2.2): a purchase and upkeep pay the civ's
 *        own people, a garrison abroad pays the locals, an unpaid bill is
 *        arrears rather than a negative treasury, and plunder is a transfer
 *        out of the loser's pockets. Phase B: coin-sweep tests removed
 *        (sweepCoins -> monetiseGoods stub, no minting).
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/monetary/MonetaryActions.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/city/ProductionSystem.hpp"
#include "aoc/simulation/economy/Maintenance.hpp"
#include "aoc/simulation/monetary/MoneyFlow.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/resource/EconomySimulation.hpp"

using aoc::PlayerId;
using aoc::sim::MoneyFlow;
using aoc::sim::MoneyLedger;
namespace {

constexpr PlayerId P0{0};
constexpr PlayerId P1{1};
constexpr aoc::UnitTypeId WARRIOR{0};

} // namespace

TEST_CASE(
    "a domestic tax draws private money and books what the people could not pay as unbacked") {
    aoc::test::World w   = aoc::test::makeWorld(1);
    aoc::game::Player& p = *w.gameState.player(P0);
    MoneyLedger ledger;
    p.setMoneyLedger(&ledger);
    p.monetary().privateSpecie               = 30;
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

TEST_CASE(
    "a transfer between treasuries books nothing; external, loss and unbacked flows are booked") {
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
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    aoc::test::addCityAt(w, P1, 14, 8, "Beta");
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
    CHECK_FALSE(
        aoc::sim::moneyConserved(before, aoc::sim::worldMoney(w.gameState), economy.moneyLedger()));
}

TEST_CASE("a purchase pays the city's own people: the money stays in the civ") {
    aoc::test::World w     = aoc::test::makeWorld(1);
    aoc::game::City& alpha = aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    aoc::game::Player& p   = *w.gameState.player(P0);
    p.monetary().system    = aoc::sim::MonetarySystemType::CommodityMoney;
    p.setTreasury(1000, MoneyFlow::external());
    MoneyLedger ledger;
    p.setMoneyLedger(&ledger);
    const int64_t before = aoc::sim::worldMoney(w.gameState);

    REQUIRE(aoc::sim::purchaseInCity(w.gameState, p, alpha, aoc::sim::ProductionItemType::Unit,
                                     WARRIOR.value) == aoc::ErrorCode::Ok);
    const int64_t price = 1000 - p.treasury();
    CHECK(price > 0);
    CHECK(p.monetary().privateSpecie == price);
    CHECK(aoc::sim::worldMoney(w.gameState) == before);
    CHECK(ledger.backed());
}

TEST_CASE("upkeep is paid where the unit stands: at home to our people, abroad to theirs") {
    aoc::test::World w   = aoc::test::makeWorld(2);
    aoc::game::Player& a = *w.gameState.player(P0);
    aoc::game::Player& b = *w.gameState.player(P1);
    a.monetary().system  = aoc::sim::MonetarySystemType::CommodityMoney;
    a.setTreasury(100, MoneyFlow::external());
    w.grid.setOwner(w.grid.toIndex(aoc::hex::AxialCoord{5, 5}), P0);
    w.grid.setOwner(w.grid.toIndex(aoc::hex::AxialCoord{14, 8}), P1);
    aoc::test::addUnitAt(w, P0, WARRIOR, 5, 5);  // garrison at home
    aoc::test::addUnitAt(w, P0, WARRIOR, 14, 8); // garrison in P1's province
    const int64_t upkeep = aoc::sim::unitTypeDef(WARRIOR).maintenanceGold();
    REQUIRE(upkeep > 0);
    const int64_t before = aoc::sim::worldMoney(w.gameState);

    CHECK(aoc::sim::processUnitMaintenance(w.gameState, w.grid, a) == 0);
    CHECK(a.treasury() == 100 - 2 * upkeep);
    CHECK(a.monetary().privateSpecie == upkeep);
    CHECK(b.monetary().privateSpecie == upkeep); // the Germania effect
    CHECK(aoc::sim::worldMoney(w.gameState) == before);
    CHECK(a.monetary().consecutiveNegativeTurns == 0);
}

TEST_CASE("an unpaid bill is arrears, never a negative treasury; five turns of it cost a unit") {
    aoc::test::World w   = aoc::test::makeWorld(1);
    aoc::game::Player& a = *w.gameState.player(P0);
    a.monetary().system  = aoc::sim::MonetarySystemType::CommodityMoney;
    for (int32_t i = 0; i < 3; ++i) {
        aoc::test::addUnitAt(w, P0, WARRIOR, 5 + i, 5);
    }
    const int64_t upkeep = aoc::sim::unitTypeDef(WARRIOR).maintenanceGold();
    a.setTreasury(upkeep, MoneyFlow::external()); // enough for one of the three
    const float taxBefore = a.monetary().taxRate;

    CHECK(aoc::sim::processUnitMaintenance(w.gameState, w.grid, a) == 2 * upkeep);
    CHECK(a.treasury() == 0);
    CHECK(a.monetary().consecutiveNegativeTurns == 1);
    CHECK(a.units().size() == 3);
    for (int32_t turn = 2; turn <= 4; ++turn) {
        CHECK(aoc::sim::processUnitMaintenance(w.gameState, w.grid, a) == 3 * upkeep);
    }
    CHECK(a.units().size() == 3); // grace
    CHECK(aoc::sim::processUnitMaintenance(w.gameState, w.grid, a) == 3 * upkeep);
    CHECK(a.units().size() == 2); // the fifth turn in arrears: one lot walks, the garrison stays
    // And arrears no longer reach for the tax lever: with money conserved, a
    // state in arrears is one whose people hold little coin, so a forced rate
    // collected nothing and cost two amenities a city.
    CHECK(a.monetary().taxRate == doctest::Approx(taxBefore));
    CHECK(a.monetary().consecutiveNegativeTurns == 0);
    CHECK(a.treasury() == 0);

    // Paid in full again, the counter clears.
    a.setTreasury(1000, MoneyFlow::external());
    CHECK(aoc::sim::processUnitMaintenance(w.gameState, w.grid, a) == 0);
    CHECK(a.monetary().consecutiveNegativeTurns == 0);
}

TEST_CASE(
    "plunder comes out of the loser's pockets first, then its treasury; nobody's is external") {
    aoc::test::World w   = aoc::test::makeWorld(2);
    aoc::game::Player& a = *w.gameState.player(P0);
    aoc::game::Player& b = *w.gameState.player(P1);
    MoneyLedger ledger;
    a.setMoneyLedger(&ledger);
    b.setMoneyLedger(&ledger);
    b.setTreasury(100, MoneyFlow::external());
    b.monetary().privateSpecie = 10;
    const int64_t before       = aoc::sim::worldMoney(w.gameState);

    CHECK(aoc::sim::plunder(w.gameState, P1, a, 30) == 30);
    CHECK(b.monetary().privateSpecie == 0);
    CHECK(b.treasury() == 80);
    CHECK(a.treasury() == 30);
    CHECK(aoc::sim::worldMoney(w.gameState) == before);
    CHECK(aoc::sim::plunder(w.gameState, P1, a, 500) == 80); // no more than they have
    CHECK(b.treasury() == 0);

    CHECK(aoc::sim::plunder(w.gameState, aoc::BARBARIAN_PLAYER, a, 25) == 25);
    CHECK(ledger.civs[0].externalIn == 25);  // the camp's hoard
    CHECK(ledger.civs[1].externalIn == 100); // the endowment above
}

TEST_CASE("printed notes and the Gold Standard's issue keep the invariant: booked as printed") {
    aoc::test::World w   = aoc::test::makeWorld(1);
    aoc::game::Player& p = *w.gameState.player(P0);
    MoneyLedger ledger;
    p.setMoneyLedger(&ledger);
    p.monetary().system        = aoc::sim::MonetarySystemType::FiatMoney;
    p.monetary().gdp           = 1000;
    p.monetary().privateSpecie = 40;
    const int64_t before       = aoc::sim::worldMoney(w.gameState);

    const aoc::CurrencyAmount issued = p.monetary().printMoney(50);
    CHECK(issued == 50);
    p.addGold(issued, MoneyFlow::printed());
    CHECK(ledger.civs[0].printed == 50);
    CHECK(aoc::sim::moneyConserved(before, aoc::sim::worldMoney(w.gameState), ledger));

    // Under paper the state pays its people in notes and taxes the notes first.
    p.addGold(-30, MoneyFlow::domestic(P0));
    CHECK(p.monetary().privateNotes == 30);
    CHECK(p.monetary().privateSpecie == 40);
    p.addGold(50, MoneyFlow::domestic(P0));
    CHECK(p.monetary().privateNotes == 0);
    CHECK(p.monetary().privateSpecie == 20);
    CHECK(aoc::sim::moneyConserved(before, aoc::sim::worldMoney(w.gameState), ledger));
}

TEST_CASE("a levy on a paper civ takes its notes before its coin, and neither pool goes negative") {
    aoc::test::World w         = aoc::test::makeWorld(2);
    aoc::game::Player& a       = *w.gameState.player(P0);
    aoc::game::Player& b       = *w.gameState.player(P1);
    b.monetary().system        = aoc::sim::MonetarySystemType::FiatMoney;
    b.monetary().privateSpecie = 100;
    b.monetary().privateNotes  = 1000;

    // A levy the note float covers never reaches the coin.
    CHECK(aoc::sim::takeFromPrivate(w.gameState, P1, a, 600) == 600);
    CHECK(b.monetary().privateNotes == 400);
    CHECK(b.monetary().privateSpecie == 100);

    // Capacity is notes plus coin, so the remaining 500 is takeable and empties
    // both. Drawing the whole sum from specie alone was what drove a fiat civ's
    // circulation below zero while its notes still stood.
    CHECK(aoc::sim::takeFromPrivate(w.gameState, P1, a, 500) == 500);
    CHECK(b.monetary().privateNotes == 0);
    CHECK(b.monetary().privateSpecie == 0);
    CHECK(a.treasury() == 1100);

    // A further levy finds the pools empty rather than pushing coin negative.
    CHECK(aoc::sim::takeFromPrivate(w.gameState, P1, a, 50) == 0);
    CHECK(b.monetary().privateSpecie == 0);
}

TEST_CASE("a tithe or a levy draws what the people hold and no more") {
    aoc::test::World w         = aoc::test::makeWorld(2);
    aoc::game::Player& a       = *w.gameState.player(P0);
    aoc::game::Player& b       = *w.gameState.player(P1);
    a.monetary().privateSpecie = 4;
    b.monetary().privateSpecie = 7;
    CHECK(aoc::sim::takeFromPrivate(a, 10) == 4);
    CHECK(a.treasury() == 4);
    CHECK(a.monetary().privateSpecie == 0);
    CHECK(aoc::sim::takeFromPrivate(w.gameState, P1, a, 5) == 5);
    CHECK(b.monetary().privateSpecie == 2);
    CHECK(a.treasury() == 9);
    CHECK(aoc::sim::takeFromPrivate(w.gameState, aoc::INVALID_PLAYER, a, 5) == 0);
    CHECK(aoc::sim::payFromTreasury(a, 100) == 9); // and the state cannot overdraw either
    CHECK(a.treasury() == 0);
    CHECK(a.monetary().privateSpecie == 9);
}

TEST_CASE("industry reclaims the money metal from coin at par, all or nothing, and the books balance") {
    aoc::test::World w     = aoc::test::makeWorld(2);
    aoc::game::Player& p   = *w.gameState.player(P1); // player 0 is the human seat
    aoc::game::City& mill  = aoc::test::addCityAt(w, P1, 14, 8, "Beta");
    aoc::game::City& other = aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    p.monetary().system    = aoc::sim::MonetarySystemType::CommodityMoney;
    p.monetary().moneyGood = static_cast<uint8_t>(aoc::sim::goods::COPPER_ORE);
    MoneyLedger ledger;
    p.setMoneyLedger(&ledger);
    const int64_t par = aoc::sim::goodDef(aoc::sim::goods::COPPER_ORE).basePrice;

    SUBCASE("coin becomes metal in the city that is short") {
        p.monetary().privateSpecie = 500;
        const int64_t before       = aoc::sim::worldMoney(w.gameState);
        CHECK(aoc::sim::reclaimMoneyMetal(p, mill, aoc::sim::goods::COPPER_ORE, 3) == 3);
        CHECK(mill.stockpile().getAmount(aoc::sim::goods::COPPER_ORE) == 3);
        CHECK(p.monetary().privateSpecie == 500 - 3 * par);
        CHECK(ledger.civs[1].monetised == -3 * par);
        CHECK(aoc::sim::moneyConserved(before, aoc::sim::worldMoney(w.gameState), ledger));
    }
    SUBCASE("coin below the cost of the shortfall buys nothing, and no pool goes negative") {
        p.monetary().privateSpecie = 2 * par;
        CHECK(aoc::sim::reclaimMoneyMetal(p, mill, aoc::sim::goods::COPPER_ORE, 3) == 0);
        CHECK(mill.stockpile().getAmount(aoc::sim::goods::COPPER_ORE) == 0);
        CHECK(p.monetary().privateSpecie == 2 * par);
        CHECK(ledger.civs[1].monetised == 0);
    }
    SUBCASE("a good that is not this civ's money, another civ's city, or a paper civ: nothing") {
        p.monetary().privateSpecie = 500;
        CHECK(aoc::sim::reclaimMoneyMetal(p, mill, aoc::sim::goods::IRON_ORE, 3) == 0);
        CHECK(aoc::sim::reclaimMoneyMetal(p, other, aoc::sim::goods::COPPER_ORE, 3) == 0);
        p.monetary().system = aoc::sim::MonetarySystemType::FiatMoney;
        CHECK(aoc::sim::reclaimMoneyMetal(p, mill, aoc::sim::goods::COPPER_ORE, 3) == 0);
        CHECK(p.monetary().privateSpecie == 500);
    }
}

TEST_CASE("a Forge short of copper draws it back out of the people's coin during the economy step") {
    aoc::test::World w    = aoc::test::makeWorld(2);
    aoc::game::Player& p  = *w.gameState.player(P1);
    aoc::game::City& mill = aoc::test::addCityAt(w, P1, 14, 8, "Beta");
    // Building 0 is the Forge; recipe 1 draws copper wire from 2 copper ore.
    mill.districts().districts.push_back(
        {aoc::sim::DistrictType::Industrial, mill.location(), {aoc::BuildingId{0}}});
    mill.setPopulation(10); // recipe slots are half the population
    REQUIRE(aoc::sim::industrialDrawFor(w.gameState, P1, aoc::sim::goods::COPPER_ORE) > 0);
    p.monetary().system        = aoc::sim::MonetarySystemType::CommodityMoney;
    p.monetary().moneyGood     = static_cast<uint8_t>(aoc::sim::goods::COPPER_ORE);
    p.monetary().privateSpecie = 1000;
    mill.stockpile().addGoods(aoc::sim::goods::COPPER_ORE, 1); // one short of a batch

    aoc::sim::EconomySimulation economy;
    economy.initialize(); // builds the recipe order; without it no recipe runs
    for (const std::unique_ptr<aoc::game::Player>& player : w.gameState.players()) {
        player->setMoneyLedger(&economy.moneyLedger());
    }
    const int64_t before = aoc::sim::worldMoney(w.gameState);
    economy.executeTurn(w.gameState, w.grid);
    const MoneyLedger::Civ& book = economy.moneyLedger().civs[1];
    REQUIRE(book.monetised < 0); // something was reclaimed, so the case is not vacuous
    CHECK(p.monetary().privateSpecie < 1000);
    CHECK(aoc::sim::moneyConserved(before, aoc::sim::worldMoney(w.gameState),
                                   economy.moneyLedger()));
}
