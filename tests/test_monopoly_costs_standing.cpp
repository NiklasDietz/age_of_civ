/**
 * @file test_monopoly_costs_standing.cpp
 * @brief Cornering a good is a choice with a price attached.
 *
 *        Detection was the game's job and the markup the player's, but no
 *        player could choose one: priceMultiplier started at 1.0, reset to 1.0
 *        on formation and on lapse, clamped DOWN to a fallen ceiling, and rose
 *        only inside requestSetMonopolyPrice, which had no caller anywhere.
 *        So it was permanently 1.0, buyerPriceMultiplier always returned 1.0,
 *        the markup at the trader delivery was a no-op, and monopolyIncome was
 *        always exactly zero.
 *
 *        Simply handing the decision to the AI would not have fixed it. With no
 *        cost on the other side, a rational monopolist charges the ceiling every
 *        time, which is the automatic share-derived markup that was deliberately
 *        removed. So squeezing now earns the monopolist a PriceGouged grievance
 *        from each buyer, and the AI charges in proportion to how little
 *        standing it has left to lose.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/simulation/diplomacy/Grievance.hpp"
#include "aoc/simulation/economy/MonopolyPricing.hpp"

using aoc::sim::GrievanceType;

namespace {

/// A tracked monopoly on `goodId` held by `holder` at the given ceiling.
aoc::sim::GlobalMonopolyComponent monopolyHeldBy(aoc::PlayerId holder, uint16_t goodId,
                                                 float ceiling) {
    aoc::sim::GlobalMonopolyComponent mono;
    mono.trackedCount                     = 1;
    mono.monopolies[0].goodId             = goodId;
    mono.monopolies[0].monopolist         = holder;
    mono.monopolies[0].isActive           = true;
    mono.monopolies[0].controlShare       = 0.75f;
    mono.monopolies[0].maxPriceMultiplier = ceiling;
    return mono;
}

} // namespace

TEST_CASE("a monopoly pays nothing until someone chooses to charge") {
    aoc::sim::GlobalMonopolyComponent mono = monopolyHeldBy(aoc::PlayerId{1}, 1, 2.0f);

    // Freshly detected: the ceiling is set, the markup is not.
    CHECK(mono.monopolies[0].priceMultiplier == doctest::Approx(1.0f));
    CHECK(mono.monopolyIncome(aoc::PlayerId{1}) == 0);
    CHECK(mono.buyerPriceMultiplier(1, aoc::PlayerId{2}) == doctest::Approx(1.0f));

    REQUIRE(aoc::sim::requestSetMonopolyPrice(mono, aoc::PlayerId{1}, 1, 2.0f) ==
            aoc::ErrorCode::Ok);

    CHECK(mono.monopolyIncome(aoc::PlayerId{1}) > 0);
    CHECK(mono.buyerPriceMultiplier(1, aoc::PlayerId{2}) == doctest::Approx(2.0f));
}

TEST_CASE("the markup is clamped to what the control share earns") {
    aoc::sim::GlobalMonopolyComponent mono = monopolyHeldBy(aoc::PlayerId{1}, 1, 1.5f);

    REQUIRE(aoc::sim::requestSetMonopolyPrice(mono, aoc::PlayerId{1}, 1, 9.0f) ==
            aoc::ErrorCode::Ok);
    CHECK(mono.monopolies[0].priceMultiplier == doctest::Approx(1.5f));

    // And never below parity: undercutting yourself is not a monopoly power.
    REQUIRE(aoc::sim::requestSetMonopolyPrice(mono, aoc::PlayerId{1}, 1, 0.2f) ==
            aoc::ErrorCode::Ok);
    CHECK(mono.monopolies[0].priceMultiplier == doctest::Approx(1.0f));
}

TEST_CASE("only the holder may price it") {
    aoc::sim::GlobalMonopolyComponent mono = monopolyHeldBy(aoc::PlayerId{1}, 1, 2.0f);

    CHECK(aoc::sim::requestSetMonopolyPrice(mono, aoc::PlayerId{2}, 1, 2.0f) != aoc::ErrorCode::Ok);
    CHECK(mono.monopolies[0].priceMultiplier == doctest::Approx(1.0f));
    // A good nobody has cornered cannot be priced either.
    CHECK(aoc::sim::requestSetMonopolyPrice(mono, aoc::PlayerId{1}, 77, 2.0f) !=
          aoc::ErrorCode::Ok);
}

TEST_CASE("the monopolist is identifiable so the grievance can find them") {
    aoc::sim::GlobalMonopolyComponent mono = monopolyHeldBy(aoc::PlayerId{3}, 5, 2.0f);
    CHECK(mono.monopolistOf(5) == aoc::PlayerId{3});
    CHECK(mono.monopolistOf(6) == aoc::INVALID_PLAYER);

    // A lapsed monopoly has no holder to blame.
    mono.monopolies[0].isActive = false;
    CHECK(mono.monopolistOf(5) == aoc::INVALID_PLAYER);
}

TEST_CASE("being gouged is an injury, and a standing markup is only one") {
    aoc::sim::PlayerGrievanceComponent g;
    g.owner = aoc::PlayerId{2};

    g.addGrievance(GrievanceType::PriceGouged, aoc::PlayerId{1});
    CHECK(g.totalGrievanceAgainst(aoc::PlayerId{1}) < 0);
    // It counts toward the combined-stress revolt gate, unlike merely
    // disliking someone's government.
    CHECK(g.injuryGrievanceCount() == 1);

    // Charged on every delivery, so repeats must refresh rather than stack --
    // otherwise one standing markup becomes unbounded hatred in a few turns.
    const int32_t afterOne = g.totalGrievanceAgainst(aoc::PlayerId{1});
    for (int32_t i = 0; i < 20; ++i) {
        g.addGrievance(GrievanceType::PriceGouged, aoc::PlayerId{1});
    }
    CHECK(g.grievances.size() == 1);
    CHECK(g.totalGrievanceAgainst(aoc::PlayerId{1}) == afterOne);
}

TEST_CASE("a gouging grievance expires once the squeezing stops") {
    aoc::sim::PlayerGrievanceComponent g;
    g.addGrievance(GrievanceType::PriceGouged, aoc::PlayerId{1});
    REQUIRE(g.grievances.size() == 1);

    for (int32_t t = 0; t < 41; ++t) {
        g.tickGrievances();
    }
    CHECK(g.grievances.empty());
}
