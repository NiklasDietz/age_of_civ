/**
 * @file test_economy_invariants.cpp
 * @brief Money-flow pinning for the salvaged economy fixes: bond issuance
 *        conserves total treasury, a defaulted bond disappears from BOTH
 *        portfolios (no phantom interest), and the wired civic effects
 *        actually mutate state.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/game/City.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/monetary/Bonds.hpp"
#include "aoc/simulation/tech/CivicEffects.hpp"

TEST_CASE("bond issue moves principal buyer->issuer and conserves the total") {
    aoc::game::GameState gs;
    gs.initialize(2);
    aoc::game::Player& issuer = *gs.players()[0];
    aoc::game::Player& holder = *gs.players()[1];
    issuer.monetary().treasury = 50;
    holder.monetary().treasury = 1000;
    const aoc::CurrencyAmount totalBefore =
        issuer.monetary().treasury + holder.monetary().treasury;

    REQUIRE(aoc::sim::issueBond(gs, aoc::PlayerId{0}, aoc::PlayerId{1}, 200)
            == aoc::ErrorCode::Ok);

    CHECK(issuer.monetary().treasury == 250);
    CHECK(holder.monetary().treasury == 800);
    CHECK(issuer.monetary().treasury + holder.monetary().treasury == totalBefore);
    REQUIRE(issuer.bonds().issuedBonds.size() == 1);
    REQUIRE(holder.bonds().heldBonds.size() == 1);
    CHECK(issuer.bonds().issuedBonds[0].id == holder.bonds().heldBonds[0].id);
}

TEST_CASE("bond default clears the bond from BOTH portfolios") {
    aoc::game::GameState gs;
    gs.initialize(2);
    aoc::game::Player& issuer = *gs.players()[0];
    aoc::game::Player& holder = *gs.players()[1];
    issuer.monetary().treasury = 0;
    holder.monetary().treasury = 1000;

    REQUIRE(aoc::sim::issueBond(gs, aoc::PlayerId{0}, aoc::PlayerId{1}, 200)
            == aoc::ErrorCode::Ok);
    REQUIRE(holder.bonds().heldBonds.size() == 1);

    // Drain the issuer and run past maturity (turnsToMaturity defaults to
    // 10): the issuer cannot pay, so the bond defaults.
    issuer.monetary().treasury = 0;
    for (int i = 0; i < 12; ++i) {
        aoc::sim::processBondPayments(gs);
    }

    CHECK(issuer.bonds().issuedBonds.empty());
    // The fix under test: before, the defaulted bond lingered in the
    // holder's heldBonds accruing phantom interest forever.
    CHECK(holder.bonds().heldBonds.empty());
}

TEST_CASE("civic LoyaltyBoost (civic 6) raises loyalty in all cities, capped") {
    aoc::game::GameState gs;
    gs.initialize(2);
    aoc::game::Player& p0 = *gs.players()[0];
    aoc::game::City& low  = p0.addCity({3, 3}, "Low");
    aoc::game::City& high = p0.addCity({8, 8}, "High");
    low.loyalty().loyalty  = 50.0f;
    high.loyalty().loyalty = 95.0f;

    aoc::sim::applyCivicEffect(gs, aoc::PlayerId{0}, /*civicId=*/6);

    CHECK(low.loyalty().loyalty == doctest::Approx(60.0f));
    CHECK(high.loyalty().loyalty == doctest::Approx(100.0f));

    // Other player's cities untouched by scoping.
    aoc::game::Player& p1 = *gs.players()[1];
    aoc::game::City& other = p1.addCity({10, 3}, "Other");
    other.loyalty().loyalty = 40.0f;
    aoc::sim::applyCivicEffect(gs, aoc::PlayerId{0}, 6);
    CHECK(other.loyalty().loyalty == doctest::Approx(40.0f));
}
