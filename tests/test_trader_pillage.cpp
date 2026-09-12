/**
 * @file test_trader_pillage.cpp
 * @brief Killing a laden Trader transfers its cargo to the killer instead of
 *        voiding it, and its purse to the killer's people; a barbarian kill
 *        takes the purse out of the world and books the owner's loss (plan 2.4).
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/economy/TradeRouteSystem.hpp"
#include "aoc/simulation/monetary/MoneyFlow.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"

using aoc::CurrencyAmount;
using aoc::PlayerId;
using aoc::UnitTypeId;

namespace {

constexpr UnitTypeId TRADER{30};
constexpr uint16_t CARGO_GOOD         = aoc::sim::goods::WINE;
constexpr int32_t CARGO_QTY           = 7;
constexpr CurrencyAmount CARRIED_COIN = 40;

/// A laden trader owned by player 1, standing where player 0 can kill it.
aoc::game::Unit& ladenTrader(aoc::test::World& w) {
    aoc::game::Unit& t            = aoc::test::addUnitAt(w, PlayerId{1}, TRADER, 6, 5);
    aoc::sim::TraderComponent& tc = t.trader();
    tc.owner                      = PlayerId{1};
    tc.cargo.push_back({CARGO_GOOD, CARGO_QTY});
    tc.carriedGold = CARRIED_COIN;
    return t;
}

} // namespace

TEST_CASE("the Trader line is detected by class, not by a single id") {
    // Combat picks traders out by UnitClass so a later row of the line is
    // covered too. Pinning id 30 alone would miss the Caravan.
    CHECK(aoc::sim::unitTypeDef(UnitTypeId{30}).unitClass == aoc::sim::UnitClass::Trader);
    CHECK(aoc::sim::unitTypeDef(UnitTypeId{31}).unitClass == aoc::sim::UnitClass::Trader);
}

TEST_CASE("looting a trader moves its cargo to the killer's city and its purse to the killer's people") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha"); // killer's receiving city
    aoc::test::addCityAt(w, PlayerId{1}, 15, 9, "Beta");
    aoc::game::Unit& trader = ladenTrader(w);

    aoc::game::Player& killer       = *w.gameState.player(PlayerId{0});
    aoc::game::City& receiving      = *killer.cities()[0];
    const int32_t goodsBefore       = receiving.stockpile().getAmount(CARGO_GOOD);
    const CurrencyAmount coinBefore = killer.monetary().treasury;
    const int64_t worldBefore       = aoc::sim::worldMoney(w.gameState);

    const CurrencyAmount looted = aoc::sim::lootTraderCargo(w.gameState, trader, PlayerId{0});

    // The goods arrived in the killer's city, not into nothing.
    CHECK(receiving.stockpile().getAmount(CARGO_GOOD) == goodsBefore + CARGO_QTY);
    // The purse went to the soldiers who took it, not to the state.
    CHECK(killer.monetary().privateSpecie == CARRIED_COIN);
    CHECK(killer.monetary().treasury == coinBefore);
    CHECK(trader.trader().carriedGold == 0);
    CHECK(aoc::sim::worldMoney(w.gameState) == worldBefore); // moved, not made
    // And the reported value covers both.
    CHECK(looted > 0);
    CHECK(looted >= CARRIED_COIN);
}

TEST_CASE("a barbarian kill takes the purse out of the world, and the owner books the loss") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{1}, 15, 9, "Beta");
    aoc::game::Unit& trader = ladenTrader(w);
    aoc::game::Player& victim = *w.gameState.player(PlayerId{1});
    aoc::sim::MoneyLedger ledger;
    victim.setMoneyLedger(&ledger);
    const int64_t worldBefore = aoc::sim::worldMoney(w.gameState);

    aoc::sim::lootTraderCargo(w.gameState, trader, aoc::BARBARIAN_PLAYER);

    CHECK(trader.trader().carriedGold == 0);
    CHECK(ledger.civs[1].lost == CARRIED_COIN);
    CHECK(aoc::sim::worldMoney(w.gameState) == worldBefore - CARRIED_COIN);
    CHECK(aoc::sim::moneyConserved(worldBefore, aoc::sim::worldMoney(w.gameState), ledger));
}

TEST_CASE("a trader deleted with coin aboard loses it, and the loss is on the books") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{1}, 15, 9, "Beta");
    aoc::game::Unit& trader = ladenTrader(w);
    aoc::game::Player& owner = *w.gameState.player(PlayerId{1});
    aoc::sim::MoneyLedger ledger;
    owner.setMoneyLedger(&ledger);
    const int64_t worldBefore = aoc::sim::worldMoney(w.gameState);
    owner.removeUnit(&trader);
    CHECK(ledger.civs[1].lost == CARRIED_COIN);
    CHECK(aoc::sim::moneyConserved(worldBefore, aoc::sim::worldMoney(w.gameState), ledger));
}

TEST_CASE("looting does not remove the unit -- the caller owns that") {
    // The combat kill path removes its dead in one deferred pass. If the loot
    // step removed the trader too it would be freed twice.
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha");
    aoc::test::addCityAt(w, PlayerId{1}, 15, 9, "Beta");
    aoc::game::Unit& trader = ladenTrader(w);

    aoc::game::Player& victim     = *w.gameState.player(PlayerId{1});
    const std::size_t unitsBefore = victim.units().size();

    [[maybe_unused]] const CurrencyAmount looted =
        aoc::sim::lootTraderCargo(w.gameState, trader, PlayerId{0});

    CHECK(victim.units().size() == unitsBefore);
}

TEST_CASE("an empty trader yields nothing and costs nothing") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha");
    aoc::test::addCityAt(w, PlayerId{1}, 15, 9, "Beta");
    aoc::game::Unit& trader = aoc::test::addUnitAt(w, PlayerId{1}, TRADER, 6, 5);
    trader.trader().owner   = PlayerId{1};

    aoc::game::Player& killer       = *w.gameState.player(PlayerId{0});
    const CurrencyAmount coinBefore = killer.monetary().treasury;

    const CurrencyAmount looted = aoc::sim::lootTraderCargo(w.gameState, trader, PlayerId{0});

    CHECK(looted == 0);
    CHECK(killer.monetary().treasury == coinBefore);
    CHECK(killer.monetary().privateSpecie == 0);
}

TEST_CASE("a killer with no city still takes the coin") {
    // There is nowhere to put crates, but the coin is on the trader's person.
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{1}, 15, 9, "Beta");
    aoc::game::Unit& trader = ladenTrader(w);

    aoc::game::Player& killer = *w.gameState.player(PlayerId{0});
    REQUIRE(killer.cities().empty());

    const CurrencyAmount looted = aoc::sim::lootTraderCargo(w.gameState, trader, PlayerId{0});
    CHECK(looted >= CARRIED_COIN); // value is still reported
    CHECK(killer.monetary().privateSpecie == CARRIED_COIN); // and the coin is with the soldiers
}
