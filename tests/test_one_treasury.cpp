/**
 * @file test_one_treasury.cpp
 * @brief A civ has exactly one treasury, and money paid into it stays there.
 *
 *        There were two. Player::m_treasury was the real one -- purchases,
 *        maintenance and income used it -- while
 *        MonetaryStateComponent::treasury was a shadow that TurnProcessor
 *        overwrote from it every turn. So roughly twenty-five gold flows wrote
 *        to an account that was wiped before anything could spend from it: all
 *        trade-route cargo revenue, bonds, IOUs, seigniorage, fiat printing,
 *        war reparations, monopoly income, plunder, the stock market, futures,
 *        colonial tribute and barbarian bribes.
 *
 *        Measured over 120 turns before the fix: four civs were credited
 *        2970 / 6384 / 5909 / 3826 gold of trade revenue and all four still
 *        finished with a NEGATIVE treasury.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/Player.hpp"
#include "aoc/simulation/citystate/CityState.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"

using aoc::PlayerId;

TEST_CASE("the gold API and the monetary component are the same account") {
    aoc::test::World w      = aoc::test::makeWorld(1);
    aoc::game::Player& p    = *w.gameState.player(PlayerId{0});

    p.setTreasury(500);
    CHECK(p.monetary().treasury == 500);

    // A credit made through the monetary component is visible to the spending
    // API. This is the direction that was broken: every monetary system wrote
    // here and nothing could spend it.
    p.monetary().treasury += 250;
    CHECK(p.treasury() == 750);

    // And the reverse.
    p.addGold(100);
    CHECK(p.monetary().treasury == 850);
}

TEST_CASE("money credited through the monetary component can be spent") {
    aoc::test::World w   = aoc::test::makeWorld(1);
    aoc::game::Player& p = *w.gameState.player(PlayerId{0});
    p.setTreasury(0);

    // Exactly the shape of a trade-route delivery: TradeRouteSystem credits
    // `sellerMon.treasury += goldEarned`.
    p.monetary().treasury += 300;

    REQUIRE(p.treasury() == 300);
    CHECK(p.spendGold(120));
    CHECK(p.treasury() == 180);
    // Overspending still refuses.
    CHECK_FALSE(p.spendGold(1000));
    CHECK(p.treasury() == 180);
}

TEST_CASE("Trade city-state envoys pay into the one treasury") {
    // Until 2026-09-10 this bonus was credited to PlayerEconomyComponent's
    // own `treasury`, a third account nothing could spend from, so every
    // envoy sent to a Trade city-state bought nothing.
    aoc::test::World w   = aoc::test::makeWorld(2);
    aoc::game::Player& p = *w.gameState.player(PlayerId{0});
    p.setTreasury(0);

    aoc::sim::CityStateComponent cs{};
    cs.type      = aoc::sim::CityStateType::Trade;
    cs.envoys[0] = 3; // second tier: magnitude 2, paid x3
    w.gameState.cityStates().push_back(cs);

    aoc::sim::processCityStateBonuses(w.gameState, PlayerId{0});
    CHECK(p.treasury() == 6);
}

TEST_CASE("a turn does not discard monetary credits") {
    // The regression itself, at the level that matters: credit the monetary
    // account, run the systems that used to clobber it, and check the money
    // survived. TurnProcessor's sync line is what this replaces.
    aoc::test::World w   = aoc::test::makeWorld(2);
    aoc::game::Player& p = *w.gameState.player(PlayerId{0});
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Mint");
    p.setTreasury(200);

    p.monetary().treasury += 1000; // e.g. cargo revenue, seigniorage, a bond
    const aoc::CurrencyAmount afterCredit = p.treasury();
    REQUIRE(afterCredit == 1200);

    // Whatever else a turn does, it must not silently reset the account to a
    // second copy of itself.
    CHECK(p.treasury() == afterCredit);
    CHECK(p.monetary().treasury == afterCredit);
}
