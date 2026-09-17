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
#include "aoc/simulation/resource/ResourceTypes.hpp"

#include <algorithm>
#include <vector>

using aoc::sim::MONEY_MINIMUM_SALEABILITY;
using aoc::sim::saleability;
using aoc::sim::SaleabilityInputs;
namespace goods = aoc::sim::goods;

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

TEST_CASE("what spoils or is bulky per unit of value is worse money") {
    SaleabilityInputs durable    = baseline();
    SaleabilityInputs perishable = baseline();
    perishable.durability        = 20;
    CHECK(saleability(durable) > saleability(perishable));

    SaleabilityInputs dense  = baseline();
    SaleabilityInputs bulky  = baseline();
    dense.basePrice          = 25;
    bulky.basePrice          = 5;
    CHECK(saleability(dense) > saleability(bulky));

    // Density saturates at gold's: a late-era metal priced for its economy
    // can tie gold on this term, never beat it.
    SaleabilityInputs gold     = baseline();
    SaleabilityInputs titanium = baseline();
    gold.basePrice             = 25;
    titanium.basePrice         = 350;
    CHECK(saleability(titanium) == saleability(gold));
    // And the edges are harmless: a free good still scores, an absurd one fits.
    SaleabilityInputs free = baseline();
    free.basePrice         = 0;
    CHECK(saleability(free) > 0);
    SaleabilityInputs top = baseline();
    top.held              = 1000000;
    top.acceptingWeight   = 100;
    top.durability        = 100;
    top.basePrice         = 1000000;
    CHECK(saleability(top) > 0);
    CHECK(saleability(top) < 1000);
}

TEST_CASE("the incumbent keeps its place with nothing in stock; a challenger does not") {
    SaleabilityInputs coinedAway = baseline();
    coinedAway.held              = 0;
    coinedAway.isIncumbent       = true;
    SaleabilityInputs candidate  = baseline();
    candidate.held               = 0;
    CHECK(saleability(candidate) == 0);
    CHECK(saleability(coinedAway) > 0);
    // At the floor of the stock term, not above a good the civ actually holds.
    SaleabilityInputs held = baseline();
    held.held              = 1;
    CHECK(saleability(coinedAway) < saleability(held));
}

namespace {

/// One good of the real table, held in `held` units, met civs indifferent,
/// nothing consuming it, steady price: the physical terms alone decide.
[[nodiscard]] SaleabilityInputs tableGood(uint16_t goodId, int32_t held) {
    SaleabilityInputs in;
    in.held        = held;
    in.totalWeight = 100;
    in.price       = std::max(1, aoc::sim::goodDef(goodId).basePrice);
    in.durability  = aoc::sim::moneyDurability(goodId);
    in.basePrice   = aoc::sim::goodDef(goodId).basePrice;
    return in;
}

} // namespace

TEST_CASE("on the real goods table the metals rise and grain, stone and gems do not") {
    // The property the physical terms exist for, measured before they existed:
    // eight civs on two seeds elected wheat, stone, barite, rice, pearls, coffee,
    // cotton, niter, coal, fish, horses, salt, pyrite, sulfur, silk and ivory,
    // and never once gold or silver ore. Every good stays eligible; this pins
    // only the ORDER at equal holdings with nobody yet accepting anything.
    constexpr int32_t HELD = 10;
    const int32_t gold     = saleability(tableGood(goods::GOLD_ORE, HELD));
    const int32_t silver   = saleability(tableGood(goods::SILVER_ORE, HELD));
    REQUIRE(gold >= MONEY_MINIMUM_SALEABILITY);
    REQUIRE(silver >= MONEY_MINIMUM_SALEABILITY);
    CHECK(gold > silver);

    std::vector<uint16_t> aboveGold;
    std::vector<uint16_t> luxuryOrBonusAboveSilver;
    std::vector<uint16_t> eligibleBonus;
    for (uint16_t id = 0; id < goods::GOOD_COUNT; ++id) {
        const int32_t score                = saleability(tableGood(id, HELD));
        const aoc::sim::GoodCategory category = aoc::sim::goodDef(id).category;
        if (score > gold) {
            aboveGold.push_back(id);
        }
        // Noble metals the table files as luxuries (platinum, alluvial gold)
        // are metals here; the failure this guards is the pearl and the gem.
        if (score >= silver && id != goods::GOLD_ORE && id != goods::SILVER_ORE &&
            aoc::sim::moneyDurability(id) < 100 &&
            (category == aoc::sim::GoodCategory::RawLuxury ||
             category == aoc::sim::GoodCategory::RawBonus)) {
            luxuryOrBonusAboveSilver.push_back(id);
        }
        // Abundance must not rescue what rots or is heavy: test at the stock cap.
        if (category == aoc::sim::GoodCategory::RawBonus &&
            saleability(tableGood(id, 1000)) >= MONEY_MINIMUM_SALEABILITY) {
            eligibleBonus.push_back(id);
        }
    }
    CHECK(aboveGold.empty());
    CHECK(luxuryOrBonusAboveSilver.empty());
    CHECK(eligibleBonus.empty());
    for (const uint16_t id : aboveGold) {
        MESSAGE("outscores gold ore: " << aoc::sim::goodDef(id).name);
    }
    for (const uint16_t id : luxuryOrBonusAboveSilver) {
        MESSAGE("luxury or bonus good at or above silver ore: " << aoc::sim::goodDef(id).name);
    }
    for (const uint16_t id : eligibleBonus) {
        MESSAGE("bonus good eligible as money: " << aoc::sim::goodDef(id).name);
    }

    // The failure mode the override list guards: a luxury the table prices
    // above silver must still lose to it, because a pearl is not divisible.
    CHECK(saleability(tableGood(goods::PEARLS, HELD)) < silver);
    CHECK(saleability(tableGood(goods::GEMS, HELD)) < silver);
    // Liquids, gases and crude ores are not money at any stock.
    for (const uint16_t id : {goods::OIL, goods::NATURAL_GAS, goods::VMS_ORE, goods::BARITE}) {
        CHECK(saleability(tableGood(id, 1000)) < MONEY_MINIMUM_SALEABILITY);
    }
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
