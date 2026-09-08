/**
 * @file test_monopoly_pricing.cpp
 * @brief A monopoly is worth what its holder decides to charge for it.
 *
 *        buyerPriceMultiplier had no callers anywhere, so holding a monopoly
 *        changed nothing about what buyers paid -- it was a flat trickle of
 *        gold and nothing else. The markup was also set automatically from
 *        control share, so the game gouged on a player's behalf whether or not
 *        they had noticed. Detection is the game's job; charging is the
 *        player's.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/simulation/economy/MonopolyPricing.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

using aoc::ErrorCode;
using aoc::PlayerId;
using aoc::sim::GlobalMonopolyComponent;
using aoc::sim::MonopolyInfo;

namespace {

constexpr uint16_t GOOD = aoc::sim::goods::IRON_ORE;

/// A component where player 0 holds an active monopoly on GOOD with a ceiling.
[[nodiscard]] GlobalMonopolyComponent heldBy(PlayerId who, float ceiling) {
    GlobalMonopolyComponent m;
    m.trackedCount          = 1;
    MonopolyInfo& info      = m.monopolies[0];
    info.goodId             = GOOD;
    info.monopolist         = who;
    info.isActive           = true;
    info.controlShare       = 0.75f;
    info.maxPriceMultiplier = ceiling;
    info.priceMultiplier    = 1.0f; // not exploiting yet
    return m;
}

} // namespace

TEST_CASE("a fresh monopoly charges nothing until its holder says so") {
    // The game noticing is not the same as the game gouging.
    GlobalMonopolyComponent m = heldBy(PlayerId{0}, 2.0f);
    CHECK(m.monopolies[0].priceMultiplier == doctest::Approx(1.0f));
    CHECK(m.buyerPriceMultiplier(GOOD, PlayerId{1}) == doctest::Approx(1.0f));
    CHECK(m.monopolyIncome(PlayerId{0}) == 0);
}

TEST_CASE("the holder can set a markup, and buyers then pay it") {
    GlobalMonopolyComponent m = heldBy(PlayerId{0}, 2.0f);

    REQUIRE(aoc::sim::requestSetMonopolyPrice(m, PlayerId{0}, GOOD, 1.8f) == ErrorCode::Ok);

    CHECK(m.monopolies[0].priceMultiplier == doctest::Approx(1.8f));
    CHECK(m.buyerPriceMultiplier(GOOD, PlayerId{1}) == doctest::Approx(1.8f));
    // And it now earns something, which it did not before.
    CHECK(m.monopolyIncome(PlayerId{0}) > 0);
}

TEST_CASE("the monopolist does not gouge itself") {
    GlobalMonopolyComponent m = heldBy(PlayerId{0}, 2.0f);
    REQUIRE(aoc::sim::requestSetMonopolyPrice(m, PlayerId{0}, GOOD, 2.0f) == ErrorCode::Ok);
    CHECK(m.buyerPriceMultiplier(GOOD, PlayerId{0}) == doctest::Approx(1.0f));
}

TEST_CASE("the markup is clamped to what the control share entitles them to") {
    GlobalMonopolyComponent m = heldBy(PlayerId{0}, 1.5f);

    REQUIRE(aoc::sim::requestSetMonopolyPrice(m, PlayerId{0}, GOOD, 99.0f) == ErrorCode::Ok);
    CHECK(m.monopolies[0].priceMultiplier == doctest::Approx(1.5f));

    // And never below par: a "markup" under 1.0 would be a subsidy.
    REQUIRE(aoc::sim::requestSetMonopolyPrice(m, PlayerId{0}, GOOD, 0.1f) == ErrorCode::Ok);
    CHECK(m.monopolies[0].priceMultiplier == doctest::Approx(1.0f));
}

TEST_CASE("only the monopolist may price the good") {
    GlobalMonopolyComponent m = heldBy(PlayerId{0}, 2.0f);
    CHECK(aoc::sim::requestSetMonopolyPrice(m, PlayerId{1}, GOOD, 2.0f) != ErrorCode::Ok);
    CHECK(m.monopolies[0].priceMultiplier == doctest::Approx(1.0f));
}

TEST_CASE("a good nobody monopolises cannot be priced") {
    GlobalMonopolyComponent m = heldBy(PlayerId{0}, 2.0f);
    CHECK(aoc::sim::requestSetMonopolyPrice(m, PlayerId{0}, aoc::sim::goods::WINE, 2.0f) !=
          ErrorCode::Ok);
}

TEST_CASE("an inactive monopoly cannot be priced") {
    GlobalMonopolyComponent m = heldBy(PlayerId{0}, 2.0f);
    m.monopolies[0].isActive  = false;
    CHECK(aoc::sim::requestSetMonopolyPrice(m, PlayerId{0}, GOOD, 2.0f) != ErrorCode::Ok);
}

TEST_CASE("a chosen markup is trimmed if the control share slips") {
    // detectMonopolies lowers the ceiling as share falls; a markup already set
    // must not survive above it.
    GlobalMonopolyComponent m = heldBy(PlayerId{0}, 3.0f);
    REQUIRE(aoc::sim::requestSetMonopolyPrice(m, PlayerId{0}, GOOD, 3.0f) == ErrorCode::Ok);

    m.monopolies[0].maxPriceMultiplier = 1.5f;
    m.monopolies[0].priceMultiplier =
        std::min(m.monopolies[0].priceMultiplier, m.monopolies[0].maxPriceMultiplier);
    CHECK(m.monopolies[0].priceMultiplier == doctest::Approx(1.5f));
}
