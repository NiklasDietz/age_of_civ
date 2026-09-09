/**
 * @file test_gold_backing.cpp
 * @brief Gold backing measures reserves against money, and is not defined by
 *        the thing it defines.
 *
 *        The money supply was `coinWealth * (1 + goldBackingRatio)` while the
 *        backing ratio was `metalBacking / moneySupply`, both recomputed every
 *        turn in that order. Solving the loop gives r(1+r) = 1, so the ratio
 *        converged on 0.618 whatever a player did -- permanently clear of the
 *        0.40 stress threshold, until base metal passed about 45 % of coin
 *        value, at which point it sat permanently below and forced a
 *        suspension. Whether a civ kept its gold standard was decided by its
 *        copper-to-silver MIX rather than by its conduct, and the design comment
 *        describing stress as a response to over-issue could not have been true.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/simulation/monetary/CurrencyCrisis.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"

using aoc::sim::MonetarySystemType;

namespace {

/// A gold-standard civ holding the given coinage, after one backing update.
float backingWith(int32_t copper, int32_t silver, int32_t goldBars) {
    aoc::sim::MonetaryStateComponent s;
    s.system              = MonetarySystemType::GoldStandard;
    s.copperCoinReserves  = copper;
    s.silverCoinReserves  = silver;
    s.goldBarReserves     = goldBars;
    // Past the five-turn grace window after entering the gold standard, during
    // which reserves are allowed to settle before redemption dynamics apply.
    s.turnsInCurrentSystem = 20;
    // The money supply as EconomySimulation sets it for a gold standard: coin
    // value plus a statutory note issue, NOT a function of the backing ratio.
    s.moneySupply = static_cast<aoc::CurrencyAmount>(
        static_cast<float>(s.totalCoinValue()) * (1.0f + aoc::sim::GOLD_STANDARD_NOTE_ISSUE));
    aoc::sim::processReserveStress(s);
    return s.goldBackingRatio;
}

} // namespace

TEST_CASE("a fully precious coinage measures the designed half backing") {
    // Notes equal to coin value, and every coin precious: reserves are half of
    // circulating money. 0.5 is the value transitionTo sets on entry, so the
    // measured figure and the designed one finally agree.
    const float backing = backingWith(/*copper=*/0, /*silver=*/100, /*goldBars=*/0);
    CHECK(backing == doctest::Approx(0.5f).epsilon(0.01));
}

TEST_CASE("base metal in the coinage erodes the backing") {
    // The mechanic the stress threshold was written for: debasing or minting
    // copper genuinely weakens the peg, rather than the ratio sitting at an
    // algebraic fixed point.
    const float allSilver = backingWith(0, 100, 0);
    const float someCopper = backingWith(200, 100, 0);
    const float muchCopper = backingWith(800, 100, 0);

    CHECK(someCopper < allSilver);
    CHECK(muchCopper < someCopper);
}

TEST_CASE("a coinage with no precious metal at all cannot back anything") {
    CHECK(backingWith(500, 0, 0) == doctest::Approx(0.0f));
}

TEST_CASE("only the base-metal share moves the backing, not the precious mix") {
    // Silver and gold both count toward backing AND toward coin value, so
    // swapping between them cancels exactly -- the era was bimetallic and the
    // model says so. What moves the ratio is copper diluting the coinage.
    //
    // I first wrote this expecting gold bars to back more heavily than silver
    // per coin. They do not, and the arithmetic says why: with no copper the
    // ratio is X / 2X = 0.5 for any precious composition.
    CHECK(backingWith(0, 100, 0) == doctest::Approx(backingWith(0, 100, 20)).epsilon(0.01));
    CHECK(backingWith(0, 0, 50) == doctest::Approx(backingWith(0, 250, 0)).epsilon(0.01));

    // Copper is the only thing that dilutes.
    CHECK(backingWith(400, 100, 20) < backingWith(0, 100, 20));
}
