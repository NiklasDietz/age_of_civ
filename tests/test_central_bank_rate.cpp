/**
 * @file test_central_bank_rate.cpp
 * @brief The policy rate is settable through the request layer, and the bank
 *        rule leans the way its consumers require.
 *
 *        interestRate has six readers -- debt service in FiscalPolicy and
 *        CurrencyCrisis, bond yields, the forex interest differential,
 *        speculative bubble formation and popping, and the tax base via
 *        taxableMoneyShare -- and exactly two writers, neither of them a
 *        decision: the hyperinflation branch of the AI's crisis response slams
 *        it to 0.25, and a bond default adds 0.05. Outside those it sat at its
 *        0.05 default for a whole game.
 *
 *        applyCentralBankPolicy is not called per turn yet; see the header for
 *        the measured reason and the one line that enables it. These tests
 *        cover the rule and the request guard regardless, so whoever turns it on
 *        inherits something already pinned.
 *
 *        The quantity rule, fiatIssueTarget, IS live: it replaced an AI branch
 *        that printed only when the treasury was negative, which the treasury
 *        has not been able to be since unpaid bills became arrears. Measured
 *        before the fix: two of four paper civs on seeds 42/43 deflated ten
 *        percent a turn to the 0.1 price floor with nothing in circulation.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/balance/BalanceParams.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/monetary/CentralBank.hpp"
#include "aoc/simulation/monetary/Inflation.hpp"
#include "aoc/simulation/monetary/MoneyFlow.hpp"
#include "aoc/simulation/monetary/MonetaryActions.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"

#include <cmath>

using aoc::CurrencyAmount;
using aoc::sim::MonetarySystemType;

namespace {

aoc::sim::MonetaryStateComponent paperCiv(float inflation, CurrencyAmount money) {
    aoc::sim::MonetaryStateComponent s;
    s.system        = MonetarySystemType::FiatMoney;
    s.inflationRate = inflation;
    s.privateNotes  = money;
    s.gdp           = 100000; // the share-of-GDP cap is not what these cases test
    return s;
}

// Half the anchor's money demand for `population` citizens.
CurrencyAmount moneyFloor(int32_t population) {
    return static_cast<CurrencyAmount>(aoc::balance::params().priceAnchorK *
                                       static_cast<float>(population) / 2.0f);
}

} // namespace

TEST_CASE("a fiat bank issues against deflation up to the anchor's money floor") {
    const int32_t pop = 20;
    REQUIRE(moneyFloor(pop) == 250);

    // Seed 43 player 3 at turn 161: 163 in circulation, prices falling ten
    // percent a turn. The rule refills to the floor in one go.
    CHECK(aoc::sim::fiatIssueTarget(paperCiv(-0.10f, 163), 0, pop) == 250 - 163);
    // Money at or above the floor: deflation alone buys nothing more.
    CHECK(aoc::sim::fiatIssueTarget(paperCiv(-0.10f, 250), 0, pop) == 0);
    CHECK(aoc::sim::fiatIssueTarget(paperCiv(-0.10f, 900), 0, pop) == 0);
    // Nobody to price for: no floor.
    CHECK(aoc::sim::fiatIssueTarget(paperCiv(-0.10f, 0), 0, 0) == 0);
}

TEST_CASE("the deflation trigger is strict and sits at minus two percent") {
    const int32_t pop = 20;
    CHECK(aoc::sim::fiatIssueTarget(paperCiv(aoc::sim::FIAT_DEFLATION_TRIGGER, 0), 0, pop) == 0);
    CHECK(aoc::sim::fiatIssueTarget(paperCiv(-0.0201f, 0), 0, pop) == 250);
    CHECK(aoc::sim::fiatIssueTarget(paperCiv(0.0f, 0), 0, pop) == 0);
}

TEST_CASE("unpaid bills are covered in full, in an inflation too, and add to the refill") {
    const int32_t pop = 20;
    // Above any ceiling: the army is not marched into desertion to make a point.
    CHECK(aoc::sim::fiatIssueTarget(paperCiv(0.40f, 5000), 30, pop) == 30);
    CHECK(aoc::sim::fiatIssueTarget(paperCiv(-0.10f, 163), 30, pop) == (250 - 163) + 30);
    CHECK(aoc::sim::fiatIssueTarget(paperCiv(0.0f, 0), -7, pop) == 0);
}

TEST_CASE("only a paper regime has a press") {
    aoc::sim::MonetaryStateComponent s = paperCiv(-0.10f, 0);
    s.system                           = MonetarySystemType::CommodityMoney;
    CHECK(aoc::sim::fiatIssueTarget(s, 30, 20) == 0);
    s.system = MonetarySystemType::GoldStandard;
    CHECK(aoc::sim::fiatIssueTarget(s, 30, 20) == 0);
    s.system = MonetarySystemType::Digital;
    CHECK(aoc::sim::fiatIssueTarget(s, 30, 20) == 280);
}

TEST_CASE("a turn's issues share one cap, and printing itself writes no inflation") {
    aoc::sim::MonetaryStateComponent s = paperCiv(0.01f, 0);
    s.gdp                              = 1000; // cap 100 a turn
    CHECK(s.printMoney(80) == 80);
    CHECK(s.printMoney(80) == 20); // only what the first call left
    CHECK(s.printMoney(1) == 0);
    CHECK(s.printAmountThisTurn == 100);
    // computeInflation is the one writer of the rate; the press only records.
    CHECK(s.inflationRate == doctest::Approx(0.01f));

    // The counter resets every turn even when there is no GDP to price it
    // against, or the cap would refuse this civ for the rest of the game.
    aoc::sim::MonetaryStateComponent broke = paperCiv(0.0f, 0);
    broke.gdp                              = 0;
    CHECK(broke.printMoney(5) == 1);
    aoc::sim::computeInflation(broke, 0, 0, 0);
    CHECK(broke.printAmountThisTurn == 0);
}

TEST_CASE("the economy step prints for a paper civ through the request, and the books balance") {
    aoc::test::World w   = aoc::test::makeWorld(2);
    aoc::game::Player& p = *w.gameState.player(aoc::PlayerId{1}); // player 0 is the human seat
    aoc::test::addCityAt(w, aoc::PlayerId{1}, 5, 5, "Beta");
    p.monetary().system        = MonetarySystemType::FiatMoney;
    p.monetary().inflationRate = -0.10f;
    p.monetary().gdp           = 100000;
    aoc::sim::MoneyLedger ledger;
    p.setMoneyLedger(&ledger);
    const int64_t before = aoc::sim::worldMoney(w.gameState);

    const CurrencyAmount want =
        aoc::sim::fiatIssueTarget(p.monetary(), p.unpaidLastTurn(), p.totalPopulation());
    REQUIRE(want > 0);
    REQUIRE(aoc::sim::requestPrintMoney(w.gameState, aoc::PlayerId{1}, want) == aoc::ErrorCode::Ok);

    CHECK(p.treasury() == want);
    CHECK(ledger.civs[1].printed == want);
    CHECK(aoc::sim::moneyConserved(before, aoc::sim::worldMoney(w.gameState), ledger));
}

TEST_CASE("setInterestRate clamps to the policy corridor") {
    aoc::sim::MonetaryStateComponent s;
    s.system = MonetarySystemType::FiatMoney;

    aoc::sim::setInterestRate(s, 0.10f);
    CHECK(s.interestRate == doctest::Approx(0.10f));

    // Above the ceiling and below the floor both come back inside.
    aoc::sim::setInterestRate(s, 5.0f);
    CHECK(s.interestRate == doctest::Approx(0.25f));
    aoc::sim::setInterestRate(s, -1.0f);
    CHECK(s.interestRate == doctest::Approx(0.0f));
}

TEST_CASE("a cheaper rate widens the taxable share and a dearer one narrows it") {
    // This is the coupling the rate decision trades against: velocity follows
    // the rate (Inflation.cpp), and the tax base follows velocity.
    aoc::sim::MonetaryStateComponent cheap;
    cheap.system          = MonetarySystemType::FiatMoney;
    cheap.velocityOfMoney = 1.2f; // what a near-zero rate drifts toward

    aoc::sim::MonetaryStateComponent dear;
    dear.system          = MonetarySystemType::FiatMoney;
    dear.velocityOfMoney = 0.7f; // what a tight rate drifts toward

    CHECK(cheap.taxableMoneyShare() > dear.taxableMoneyShare());
    // And the calibration point is preserved: neutral velocity gives the 0.35
    // both tax sites used to hardcode.
    aoc::sim::MonetaryStateComponent neutral;
    neutral.velocityOfMoney = 1.0f;
    CHECK(neutral.taxableMoneyShare() == doctest::Approx(0.35f));
}

TEST_CASE("the inflation target sits inside the range the game actually produces") {
    // Guards the mistake this rule was first written with: a 4% target borrowed
    // from macroeconomics, against measured mean inflation of 0.0014 (seed 42)
    // and 0.0073 (seed 43). Every turn then read as a shortfall, the rule cut to
    // the floor and stayed there, and since bubbles form below 0.08 the count
    // went to 25 and 111 while seed 43's revolts reached 957.
    //
    // Pinned against the measured ceiling rather than a guess: a target above
    // the inflation a game can generate is not a target, it is a one-way ratchet
    // toward zero.
    constexpr float MEASURED_MEAN_INFLATION_SEED43 = 0.0073f;
    constexpr float TARGET                         = 0.005f;
    CHECK(TARGET < MEASURED_MEAN_INFLATION_SEED43 * 2.0f);
    CHECK(TARGET > 0.0f);
}

TEST_CASE("debt service is what stops a bank tightening freely") {
    // FiscalPolicy charges governmentDebt * interestRate every turn, so the
    // relief term in the rule is not a fudge -- it is the cost side of the
    // decision.
    aoc::sim::MonetaryStateComponent s;
    s.governmentDebt = 10000;

    aoc::sim::setInterestRate(s, 0.02f);
    const aoc::CurrencyAmount cheapService =
        static_cast<aoc::CurrencyAmount>(static_cast<float>(s.governmentDebt) * s.interestRate);

    aoc::sim::setInterestRate(s, 0.20f);
    const aoc::CurrencyAmount dearService =
        static_cast<aoc::CurrencyAmount>(static_cast<float>(s.governmentDebt) * s.interestRate);

    CHECK(dearService > cheapService);
    CHECK(dearService - cheapService == 1800);
}
