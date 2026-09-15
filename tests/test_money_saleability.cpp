/**
 * @file test_money_saleability.cpp
 * @brief Menger's saleability rule: the property that decides which good a
 *        people ends up treating as money. Each term is pinned for its sign,
 *        and the two structural properties the design rests on get a case of
 *        their own: acceptance must be able to beat abundance, and a good
 *        industry eats must lose to one it does not.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/simulation/monetary/MonetaryActions.hpp"

using aoc::sim::saleability;
using aoc::sim::SaleabilityInputs;

namespace {

/// A civ holding a decent stock of a good nobody else uses, nothing consumes,
/// and whose price is steady. Every case below moves one term off this.
[[nodiscard]] SaleabilityInputs baseline() {
    SaleabilityInputs in;
    in.held            = 50;
    in.acceptingWeight = 0;
    in.totalWeight     = 100;
    in.industrialDraw  = 0;
    in.priceSwing      = 0;
    in.price           = 10;
    return in;
}

} // namespace

TEST_CASE("a civ cannot monetise a good it does not hold") {
    SaleabilityInputs in = baseline();
    in.held              = 0;
    CHECK(saleability(in) == 0);

    // And holding any at all is enough to be in the running.
    in.held = 1;
    CHECK(saleability(in) > 0);
}

TEST_CASE("acceptance by trading partners raises saleability") {
    SaleabilityInputs alone = baseline();
    SaleabilityInputs some  = baseline();
    SaleabilityInputs all   = baseline();
    some.acceptingWeight    = 50;
    all.acceptingWeight     = 100;

    CHECK(saleability(alone) < saleability(some));
    CHECK(saleability(some) < saleability(all));
}

TEST_CASE("acceptance can beat abundance, which is the whole point") {
    // Acceptance beats abundance. A good ten times scarcer, but which the civ's
    // partners already take, has to win or money never converges on anything.
    SaleabilityInputs abundantAndIgnored = baseline();
    abundantAndIgnored.held              = 1000;
    abundantAndIgnored.acceptingWeight   = 0;

    SaleabilityInputs scarceAndAccepted = baseline();
    scarceAndAccepted.held              = 10;
    scarceAndAccepted.acceptingWeight   = 100;

    CHECK(saleability(scarceAndAccepted) > saleability(abundantAndIgnored));
}

TEST_CASE("before anyone has adopted, the more plentiful good leads") {
    // The flip side: with acceptance zero everywhere, the terms that remain are
    // stock and usefulness, so the first movers pick on those. Money has to be
    // able to start somewhere.
    SaleabilityInputs plentiful = baseline();
    SaleabilityInputs scarce    = baseline();
    plentiful.held              = 200;
    scarce.held                 = 5;

    CHECK(saleability(plentiful) > saleability(scarce));
}

TEST_CASE("a good that industry eats is worse money, and that is the road to fiat") {
    SaleabilityInputs idle     = baseline();
    SaleabilityInputs eaten    = baseline();
    SaleabilityInputs devoured = baseline();
    eaten.industrialDraw       = 25; // half its stock per turn
    devoured.industrialDraw    = 50; // all of it

    CHECK(saleability(idle) > saleability(eaten));
    CHECK(saleability(eaten) > saleability(devoured));

    // When industry takes everything the good is no use as a store at all. This
    // is the term whose collapse makes paper worth adopting, rather than a
    // scripted era event.
    CHECK(saleability(devoured) == 0);
}

TEST_CASE("an unstable price makes worse money") {
    SaleabilityInputs steady = baseline();
    SaleabilityInputs jumpy  = baseline();
    jumpy.priceSwing         = 8; // against a price of 10

    CHECK(saleability(steady) > saleability(jumpy));

    // The floor keeps a wildly swinging good in the running rather than
    // disqualifying it outright: people did use volatile money.
    SaleabilityInputs wild = baseline();
    wild.priceSwing        = 1000;
    CHECK(saleability(wild) > 0);
}

TEST_CASE("a civ that has met nobody is not barred from adopting") {
    // Acceptance is neutral rather than zero without contact, or an isolated
    // civ could never use money at all.
    SaleabilityInputs isolated = baseline();
    isolated.totalWeight       = 0;
    isolated.acceptingWeight   = 0;

    SaleabilityInputs met = baseline(); // contact, but nobody using it
    CHECK(saleability(isolated) == saleability(met));
}

TEST_CASE("the score is deterministic and order independent") {
    const SaleabilityInputs in = baseline();
    CHECK(saleability(in) == saleability(in));

    // Partner weight beyond the total does not overflow the acceptance term.
    SaleabilityInputs bogus = baseline();
    bogus.acceptingWeight   = 10000;
    bogus.totalWeight       = 100;
    SaleabilityInputs full  = baseline();
    full.acceptingWeight    = 100;
    CHECK(saleability(bogus) == saleability(full));
}
