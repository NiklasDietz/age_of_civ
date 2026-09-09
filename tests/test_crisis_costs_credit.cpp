/**
 * @file test_crisis_costs_credit.cpp
 * @brief A currency crisis damages the credit standing that the rest of the
 *        game actually reads.
 *
 *        MonetaryStateComponent used to carry its own `fiatTrust` alongside
 *        CurrencyTrustComponent::trustScore, and every penalty the crisis
 *        system levied was written to that copy. Nothing read it and the save
 *        file did not carry it, so two documented mechanics did nothing at all:
 *
 *          - The G4 post-reform trust cap, whose comment stated its purpose as
 *            stopping a civ that had just hyperinflated its debt away from
 *            immediately rebuilding reserve-currency status. Reserve status is
 *            granted off trustScore >= 0.80, which the cap never touched.
 *          - The suspension-of-convertibility penalty, which is reached by
 *            three civs per game on seed 42.
 *
 *        These tests pin the penalties to trustScore, so a regression that
 *        reintroduces a shadow field fails here rather than going unnoticed
 *        for however long the last one did.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/simulation/monetary/CurrencyCrisis.hpp"
#include "aoc/simulation/monetary/CurrencyTrust.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"

using aoc::sim::MonetarySystemType;

namespace {

/// The threshold updateReserveCurrencyStatus grants reserve status at.
constexpr float RESERVE_ACQUIRE_THRESHOLD = 0.80f;

/// A gold-standard civ whose reserves have collapsed far enough to force the
/// suspension branch, with the grace window already elapsed.
aoc::sim::MonetaryStateComponent collapsedGoldCiv() {
    aoc::sim::MonetaryStateComponent s;
    s.system               = MonetarySystemType::GoldStandard;
    s.turnsInCurrentSystem = 20;
    // Almost all base metal: backing lands under the 0.20 collapse threshold.
    s.copperCoinReserves = 1000;
    s.silverCoinReserves = 0;
    s.goldBarReserves    = 0;
    s.moneySupply = static_cast<aoc::CurrencyAmount>(static_cast<float>(s.totalCoinValue()) *
                                                     (1.0f + aoc::sim::GOLD_STANDARD_NOTE_ISSUE));
    return s;
}

} // namespace

TEST_CASE("suspending convertibility costs the civ its credit") {
    aoc::sim::MonetaryStateComponent s = collapsedGoldCiv();
    aoc::sim::CurrencyTrustComponent trust;
    const float before = trust.trustScore;

    aoc::sim::processReserveStress(s, trust);

    REQUIRE(s.system == MonetarySystemType::FiatMoney); // suspension fired
    CHECK(trust.trustScore < before);
}

TEST_CASE("the credit penalty has a floor rather than zeroing trust") {
    aoc::sim::MonetaryStateComponent s = collapsedGoldCiv();
    aoc::sim::CurrencyTrustComponent trust;
    trust.trustScore = 0.16f; // already close to the floor

    aoc::sim::processReserveStress(s, trust);

    REQUIRE(s.system == MonetarySystemType::FiatMoney);
    CHECK(trust.trustScore == doctest::Approx(0.15f));
}

TEST_CASE("a civ on a healthy gold standard keeps its credit") {
    aoc::sim::MonetaryStateComponent s;
    s.system               = MonetarySystemType::GoldStandard;
    s.turnsInCurrentSystem = 20;
    s.copperCoinReserves   = 0;
    s.silverCoinReserves   = 100;
    s.goldBarReserves      = 10;
    s.moneySupply = static_cast<aoc::CurrencyAmount>(static_cast<float>(s.totalCoinValue()) *
                                                     (1.0f + aoc::sim::GOLD_STANDARD_NOTE_ISSUE));

    aoc::sim::CurrencyTrustComponent trust;
    const float before = trust.trustScore;

    aoc::sim::processReserveStress(s, trust);

    REQUIRE(s.system == MonetarySystemType::GoldStandard); // no suspension
    CHECK(trust.trustScore == doctest::Approx(before));
}

TEST_CASE("a currency reform caps credit below the reserve-currency gate") {
    aoc::sim::MonetaryStateComponent s;
    s.system = MonetarySystemType::FiatMoney;
    aoc::sim::CurrencyCrisisComponent crisis;
    aoc::sim::CurrencyTrustComponent trust;

    // A civ with the world's most trusted currency, about to inflate away its
    // debt. This is the exploit the cap exists to close.
    trust.trustScore = 0.95f;
    s.governmentDebt = 10000;
    s.moneySupply    = 8000;

    aoc::sim::executeCurrencyReform(s, crisis, trust);

    CHECK(crisis.reformTrustCapTurns == 50);
    CHECK(trust.trustScore <= 0.30f);
    CHECK(trust.trustScore < RESERVE_ACQUIRE_THRESHOLD);
}
