/**
 * @file test_iou_term_expiry.cpp
 * @brief Pins the term-expiry force-close in processIOUPayments (aoc::sim,
 *        Bonds.hpp).
 *
 * processIOUPayments only ever removed a loan when its balance hit zero
 * (`remaining <= 0`); it never acted on `turnsRemaining <= 0`, so a loan whose
 * debtor could not fully amortize by term end lingered forever -- turnsRemaining
 * running negative while interest compounded on an orphaned contract. The fix
 * settles the full balance at term end and closes the contract regardless: the
 * debtor pays what it can, any shortfall is a default (currency-trust hit) that
 * is written off. These cases pin both outcomes.
 *
 * Note: IOU cash lives in Player::monetary().treasury, NOT Player::treasury()
 * (a separate field); the test sets and reads the monetary component.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/monetary/Bonds.hpp"

namespace {

struct IOUWorld {
    aoc::game::GameState gameState;

    IOUWorld() { gameState.initialize(2); }

    aoc::game::Player& creditor() { return *this->gameState.players()[0]; }
    aoc::game::Player& debtor() { return *this->gameState.players()[1]; }
};

} // namespace

TEST_CASE("an expired IOU is force-closed instead of lingering past its term") {
    IOUWorld w;
    w.creditor().monetary().treasury = 5000;  // enough to fund the loan

    // A short 2-turn term: expiry hits well before the 20-turn amortization
    // schedule pays the loan down, so `remaining` is still positive at term.
    REQUIRE(aoc::sim::createIOU(w.gameState, w.creditor().id(), w.debtor().id(),
                                1000, 0.05f, 2) == aoc::ErrorCode::Ok);
    REQUIRE(w.creditor().ious().loansGiven.size() == 1);
    REQUIRE(w.debtor().ious().loansReceived.size() == 1);

    SUBCASE("solvent debtor settles the balance in full, no default") {
        w.debtor().monetary().treasury = 5000;  // can cover the balloon settlement
        const float trustBefore = w.debtor().currencyTrust().trustScore;
        const aoc::CurrencyAmount creditorBefore = w.creditor().monetary().treasury;

        // Three ticks takes turnsRemaining from 2 down through 0 (term end).
        for (int32_t i = 0; i < 3; ++i) { aoc::sim::processIOUPayments(w.gameState); }

        // Contract removed from BOTH sides at term end (old code left it).
        CHECK(w.creditor().ious().loansGiven.empty());
        CHECK(w.debtor().ious().loansReceived.empty());
        // Creditor was repaid; a solvent settlement is not a default.
        CHECK(w.creditor().monetary().treasury > creditorBefore);
        CHECK(w.debtor().currencyTrust().trustScore == doctest::Approx(trustBefore));
    }

    SUBCASE("insolvent debtor defaults; the balance is written off and closed") {
        w.debtor().monetary().treasury = 0;  // spent the loan, cannot repay
        const float trustBefore = w.debtor().currencyTrust().trustScore;

        for (int32_t i = 0; i < 3; ++i) { aoc::sim::processIOUPayments(w.gameState); }

        // Force-closed despite non-payment -- old code lingered forever here.
        CHECK(w.creditor().ious().loansGiven.empty());
        CHECK(w.debtor().ious().loansReceived.empty());
        // Default is recorded as a currency-trust penalty (0.30 -> 0.20).
        CHECK(w.debtor().currencyTrust().trustScore < trustBefore);
    }
}
