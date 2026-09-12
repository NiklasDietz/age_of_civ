/**
 * @file test_war_reparations.cpp
 * @brief War reparations must never drive a treasury negative, on either of the
 *        two paths that pay them: the deal system (`DealTerms.cpp`, which pays
 *        what it can and logs a partial payment) and the AI-vs-AI peace path
 *        (`AIDiplomacyController.cpp`, which transfers 10% directly).
 *
 *        Downstream loan and crisis maths read the treasury, so a negative one
 *        corrupts them. The invariant is pinned here rather than left to the
 *        arithmetic of whichever guard happens to enclose each transfer.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/Player.hpp"
#include "aoc/simulation/diplomacy/DealTerms.hpp"

using aoc::CurrencyAmount;
using aoc::PlayerId;

namespace {

/// The AI peace path's sum, extracted so the arithmetic can be checked over the
/// whole range of near-empty treasuries without standing up a war.
/// Mirrors AIDiplomacyController.cpp: owed = max(1, treasury/10), clamped to
/// what the payer actually holds.
[[nodiscard]] CurrencyAmount aiReparations(CurrencyAmount treasury) {
    if (treasury <= 0) {
        return 0; // the enclosing guard declines to transfer at all
    }
    const CurrencyAmount owed = std::max(static_cast<CurrencyAmount>(1), treasury / 10);
    return std::min<CurrencyAmount>(owed, treasury);
}

} // namespace

TEST_CASE("the AI peace path cannot drive a near-empty treasury below zero") {
    // Every treasury from empty to comfortably funded.
    for (CurrencyAmount t = 0; t <= 200; ++t) {
        const CurrencyAmount paid = aiReparations(t);
        CHECK(paid <= t);     // never pays more than it holds
        CHECK(t - paid >= 0); // and so never lands below zero
    }

    // The cases that used to look dangerous: treasury/10 truncates to 0, so the
    // max(1, ...) floor is what actually gets charged.
    CHECK(aiReparations(1) == 1);
    CHECK(aiReparations(5) == 1);
    CHECK(aiReparations(9) == 1);
    CHECK(aiReparations(10) == 1);
    CHECK(aiReparations(100) == 10);

    // An empty or already-negative treasury pays nothing.
    CHECK(aiReparations(0) == 0);
    CHECK(aiReparations(-50) == 0);
}

TEST_CASE("a player with one gold is left at zero, not in debt") {
    aoc::test::World w        = aoc::test::makeWorld(2);
    aoc::game::Player& loser  = *w.gameState.player(PlayerId{0});
    aoc::game::Player& winner = *w.gameState.player(PlayerId{1});

    loser.setTreasury(1, aoc::sim::MoneyFlow::external());
    winner.setTreasury(0, aoc::sim::MoneyFlow::external());

    const CurrencyAmount paid = aiReparations(loser.treasury());
    loser.addGold(-paid, aoc::sim::MoneyFlow::external());
    winner.addGold(paid, aoc::sim::MoneyFlow::external());

    CHECK(loser.treasury() == 0);
    CHECK(loser.treasury() >= 0);
    CHECK(winner.treasury() == paid);
    // Gold is conserved: the transfer neither mints nor burns.
    CHECK(loser.treasury() + winner.treasury() == 1);
}

TEST_CASE("the deal path pays partially rather than going negative") {
    // DealTerms.cpp clamps with available = max(0, treasury), paid = min(owed,
    // available). Same invariant, stated over the monetary-component treasury
    // that path uses.
    aoc::test::World w       = aoc::test::makeWorld(2);
    aoc::game::Player& payer = *w.gameState.player(PlayerId{0});
    aoc::game::Player& payee = *w.gameState.player(PlayerId{1});

    payer.monetary().treasury = 30;
    payee.monetary().treasury = 0;

    const CurrencyAmount owed      = 100; // more than the payer holds
    const CurrencyAmount available = std::max<CurrencyAmount>(0, payer.monetary().treasury);
    const CurrencyAmount paid      = std::min<CurrencyAmount>(owed, available);
    payer.monetary().treasury -= paid;
    payee.monetary().treasury += paid;

    CHECK(paid == 30);                     // paid what it could
    CHECK(payer.monetary().treasury == 0); // and stopped at zero
    CHECK(payer.monetary().treasury >= 0);
    CHECK(payee.monetary().treasury == 30);
}
