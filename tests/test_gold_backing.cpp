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

#include "support/World.hpp"

#include "aoc/game/Player.hpp"
#include "aoc/simulation/monetary/CurrencyCrisis.hpp"
#include "aoc/simulation/monetary/CurrencyTrust.hpp"
#include "aoc/simulation/monetary/MoneyFlow.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"

using aoc::sim::MonetarySystemType;

namespace {

/// A gold-standard civ holding `specie` in coin and `notes` in paper, its
/// treasury empty, after one backing update past the grace window.
float backingWith(aoc::CurrencyAmount specie, aoc::CurrencyAmount notes) {
    aoc::sim::MonetaryStateComponent s;
    s.system               = MonetarySystemType::GoldStandard;
    s.privateSpecie        = specie;
    s.privateNotes         = notes;
    s.turnsInCurrentSystem = 20;
    aoc::sim::CurrencyTrustComponent trust;
    aoc::sim::processReserveStress(s, trust);
    return s.goldBackingRatio;
}

} // namespace

TEST_CASE("notes matched by the people's coin measure full backing") {
    // The issue at adoption is one for one, so entry is 1.0: the value
    // transitionTo sets, and the measured figure agrees with it.
    CHECK(backingWith(100, 100) == doctest::Approx(1.0f).epsilon(0.01));
    CHECK(backingWith(300, 100) == doctest::Approx(1.0f).epsilon(0.01)); // capped: more coin than paper
}

TEST_CASE("specie leaving the country erodes the backing, and paper without coin has none") {
    CHECK(backingWith(50, 100) == doctest::Approx(0.5f).epsilon(0.01));
    CHECK(backingWith(20, 100) == doctest::Approx(0.2f).epsilon(0.01));
    CHECK(backingWith(0, 100) == doctest::Approx(0.0f));
}

TEST_CASE("the treasury counts on both sides: it holds coin and it owes nobody paper") {
    aoc::sim::MonetaryStateComponent s;
    s.system               = MonetarySystemType::GoldStandard;
    s.privateSpecie        = 20;
    s.privateNotes         = 100;
    s.turnsInCurrentSystem = 20;
    aoc::sim::CurrencyTrustComponent trust;
    aoc::sim::processReserveStress(s, trust);
    const float poor = s.goldBackingRatio;
    aoc::sim::MonetaryStateComponent rich = s;
    aoc::sim::TreasuryAccount empty{};
    (void)empty;
    // A treasury of 80 lifts (20 + 80) / (100 + 80) above 20 / 100.
    aoc::test::World w   = aoc::test::makeWorld(1);
    aoc::game::Player& p = *w.gameState.player(aoc::PlayerId{0});
    p.monetary()         = rich;
    p.setTreasury(80, aoc::sim::MoneyFlow::external());
    aoc::sim::processReserveStress(p.monetary(), trust);
    CHECK(p.monetary().goldBackingRatio > poor);
}
