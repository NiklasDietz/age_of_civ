/**
 * @file test_monetary_actions.cpp
 * @brief The monetary levers are reachable, and refuse what they should.
 *
 *        debaseCurrency, remintCurrency and devalueCurrency were fully
 *        implemented and had ZERO callers between them. The consequences
 *        cascaded rather than staying local: because nothing ever debased,
 *        debasementRatio stayed at 0, so tickDebasementDiscovery -- which runs
 *        every turn -- could never return true, and the Gresham's-law tier
 *        routing, the discovery curve, the reputational trade penalty and the
 *        remint escape valve were all unreachable code. Because nothing ever
 *        devalued, exportPriceMultiplier always returned exactly 1.0.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/Player.hpp"
#include "aoc/simulation/monetary/MonetaryActions.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"

using aoc::ErrorCode;
using aoc::PlayerId;
using aoc::sim::MonetarySystemType;

TEST_CASE("a coinage civ can debase, and doing so dilutes the coinage") {
    aoc::test::World w   = aoc::test::makeWorld(2);
    aoc::game::Player& p = *w.gameState.player(PlayerId{0});
    p.monetary().system  = MonetarySystemType::CommodityMoney;
    REQUIRE(p.monetary().debasement.debasementRatio == doctest::Approx(0.0f));

    CHECK(aoc::sim::requestDebaseCurrency(w.gameState, PlayerId{0}, 0.05f) == ErrorCode::Ok);
    CHECK(p.monetary().debasement.debasementRatio > 0.0f);
}

TEST_CASE("a paper currency has no metal content to dilute") {
    // Debasement is a coinage act; the fiat equivalent is printing, and
    // conflating them would let a civ do both to the same money.
    aoc::test::World w   = aoc::test::makeWorld(2);
    aoc::game::Player& p = *w.gameState.player(PlayerId{0});
    p.monetary().system  = MonetarySystemType::FiatMoney;

    CHECK(aoc::sim::requestDebaseCurrency(w.gameState, PlayerId{0}, 0.05f) == ErrorCode::InvalidState);
    CHECK(p.monetary().debasement.debasementRatio == doctest::Approx(0.0f));
}

TEST_CASE("reminting costs real money, which is what makes debasement a loan") {
    // Restriking shaves 0.10 off the ratio and charges a fifth of the treasury.
    // The cost is the point: without it, debasing would be free seigniorage a
    // civ could simply undo, rather than a borrowing against its reputation.
    aoc::test::World w   = aoc::test::makeWorld(2);
    aoc::game::Player& p = *w.gameState.player(PlayerId{0});
    p.monetary().system  = MonetarySystemType::CommodityMoney;

    // Nothing to put right yet.
    CHECK(aoc::sim::requestRemintCurrency(w.gameState, PlayerId{0}) == ErrorCode::InvalidState);

    REQUIRE(aoc::sim::requestDebaseCurrency(w.gameState, PlayerId{0}, 0.10f) == ErrorCode::Ok);
    REQUIRE(p.monetary().debasement.debasementRatio > 0.0f);

    // A broke civ cannot afford to put its coinage right.
    p.setTreasury(0);
    CHECK(aoc::sim::requestRemintCurrency(w.gameState, PlayerId{0}) ==
          ErrorCode::InsufficientResources);

    p.setTreasury(1000);
    CHECK(aoc::sim::requestRemintCurrency(w.gameState, PlayerId{0}) == ErrorCode::Ok);
    CHECK(p.monetary().debasement.debasementRatio == doctest::Approx(0.0f));
    CHECK(p.treasury() == 800); // a fifth, gone
}

TEST_CASE("printing is refused outside a fiat-class system") {
    aoc::test::World w   = aoc::test::makeWorld(2);
    aoc::game::Player& p = *w.gameState.player(PlayerId{0});

    p.monetary().system = MonetarySystemType::CommodityMoney;
    CHECK(aoc::sim::requestPrintMoney(w.gameState, PlayerId{0}, 100) == ErrorCode::InvalidState);

    p.monetary().system = MonetarySystemType::FiatMoney;
    p.monetary().gdp    = 10000;
    CHECK(aoc::sim::requestPrintMoney(w.gameState, PlayerId{0}, 100) == ErrorCode::Ok);
}

TEST_CASE("a metal currency cannot be talked down") {
    // Devaluation is an act against a floating currency; there is nothing to
    // devalue when the unit is a weight of silver.
    aoc::test::World w   = aoc::test::makeWorld(2);
    aoc::game::Player& p = *w.gameState.player(PlayerId{0});
    p.monetary().system  = MonetarySystemType::GoldStandard;
    aoc::sim::GlobalCurrencyWarState war;

    CHECK(aoc::sim::requestDevalueCurrency(w.gameState, PlayerId{0}, war) == ErrorCode::InvalidState);
    CHECK_FALSE(p.currencyDevaluation().isDevalued);
}

TEST_CASE("the actions refuse a player who does not exist") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::sim::GlobalCurrencyWarState war;
    CHECK(aoc::sim::requestDebaseCurrency(w.gameState, PlayerId{200}, 0.05f) == ErrorCode::EntityNotFound);
    CHECK(aoc::sim::requestRemintCurrency(w.gameState, PlayerId{200}) == ErrorCode::EntityNotFound);
    CHECK(aoc::sim::requestPrintMoney(w.gameState, PlayerId{200}, 10) == ErrorCode::EntityNotFound);
    CHECK(aoc::sim::requestDevalueCurrency(w.gameState, PlayerId{200}, war) == ErrorCode::EntityNotFound);
}
