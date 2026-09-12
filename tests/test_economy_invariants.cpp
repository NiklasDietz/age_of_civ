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
    issuer.setTreasury(50, aoc::sim::MoneyFlow::external());
    holder.setTreasury(1000, aoc::sim::MoneyFlow::external());
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
    issuer.setTreasury(0, aoc::sim::MoneyFlow::external());
    holder.setTreasury(1000, aoc::sim::MoneyFlow::external());

    REQUIRE(aoc::sim::issueBond(gs, aoc::PlayerId{0}, aoc::PlayerId{1}, 200)
            == aoc::ErrorCode::Ok);
    REQUIRE(holder.bonds().heldBonds.size() == 1);

    // Drain the issuer and run past maturity (turnsToMaturity defaults to
    // 10): the issuer cannot pay, so the bond defaults.
    issuer.setTreasury(0, aoc::sim::MoneyFlow::external());
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

// ============================================================================
// Borrowing is recorded as debt
// ============================================================================

TEST_CASE("issuing a bond records the principal as government debt") {
    // governmentDebt used to be exactly 0 for every civ for entire games. The
    // only thing that wrote it was an auto-borrow in executeFiscalPolicy whose
    // deficit was defined as max(0.9 * revenue, 0.04 * GDP) - revenue, which
    // needs a tax rate under about 4 % to be positive; the default is 15 % and
    // the AI only raises it. Bonds and IOUs moved cash and recorded no
    // liability at all.
    //
    // That single permanent zero switched off the sovereign-default trigger
    // (governmentDebt > 0), the bank-run debt-to-gold test, the DebtSpiral
    // collapse type, the bond-yield debt premium, and pinned currency trust's
    // debtFactor at its most favourable value.
    aoc::game::GameState gs;
    gs.initialize(2);
    aoc::game::Player& issuer = *gs.players()[0];
    aoc::game::Player& holder = *gs.players()[1];
    issuer.setTreasury(50, aoc::sim::MoneyFlow::external());
    holder.setTreasury(1000, aoc::sim::MoneyFlow::external());
    REQUIRE(issuer.monetary().governmentDebt == 0);

    REQUIRE(aoc::sim::issueBond(gs, aoc::PlayerId{0}, aoc::PlayerId{1}, 200)
            == aoc::ErrorCode::Ok);

    CHECK(issuer.monetary().governmentDebt == 200);
    // The lender has not borrowed anything.
    CHECK(holder.monetary().governmentDebt == 0);
}

TEST_CASE("an IOU is debt for the borrower, not the lender") {
    aoc::game::GameState gs;
    gs.initialize(2);
    aoc::game::Player& creditor = *gs.players()[0];
    aoc::game::Player& debtor   = *gs.players()[1];
    creditor.setTreasury(1000, aoc::sim::MoneyFlow::external());
    debtor.setTreasury(10, aoc::sim::MoneyFlow::external());

    REQUIRE(aoc::sim::createIOU(gs, aoc::PlayerId{0}, aoc::PlayerId{1}, 300)
            == aoc::ErrorCode::Ok);

    CHECK(debtor.monetary().governmentDebt == 300);
    CHECK(creditor.monetary().governmentDebt == 0);
}

TEST_CASE("debt never goes negative when more is repaid than was borrowed") {
    // Repayment subtracts from the stock, and interest is not part of it, so a
    // clamp is the difference between a solvent civ and one holding a negative
    // debt that would read as an asset everywhere downstream.
    aoc::game::GameState gs;
    gs.initialize(2);
    aoc::game::Player& debtor = *gs.players()[1];
    debtor.monetary().governmentDebt = 0;
    debtor.setTreasury(5000, aoc::sim::MoneyFlow::external());
    gs.players()[0]->setTreasury(5000, aoc::sim::MoneyFlow::external());

    REQUIRE(aoc::sim::createIOU(gs, aoc::PlayerId{0}, aoc::PlayerId{1}, 100)
            == aoc::ErrorCode::Ok);
    REQUIRE(debtor.monetary().governmentDebt == 100);

    for (int32_t turn = 0; turn < 20; ++turn) {
        aoc::sim::processIOUPayments(gs);
    }
    CHECK(debtor.monetary().governmentDebt >= 0);
}
