/**
 * @file test_religion_loyalty.cpp
 * @brief Religion holds an empire together, or pulls it apart.
 *
 *        The stated intent is that religion gives a better hold on large
 *        empires and causes science friction in the modern age. The friction
 *        half existed (religionScienceCoefficient). The holding half was
 *        broken in two ways: the coefficient fell to exactly zero from the
 *        Renaissance on, switching religion off at the point empires grow
 *        large enough to need holding, and the bonus was faith-AGNOSTIC -- a
 *        city devoutly following a rival's religion propped up its owner's
 *        loyalty exactly as much as one following the owner's own.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/city/CityLoyalty.hpp"
#include "aoc/simulation/religion/Religion.hpp"

using aoc::PlayerId;
using aoc::sim::RELIGION_LOYALTY_LIMIT;

namespace {

constexpr aoc::sim::ReligionId OURS{0};
constexpr aoc::sim::ReligionId THEIRS{1};

/// A city for player 0, following `faith` with the given pressure.
aoc::game::City& cityFollowing(aoc::test::World& w, aoc::sim::ReligionId faith, float pressure) {
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha");
    aoc::game::City& city = *w.gameState.player(PlayerId{0})->cities()[0];
    if (faith != aoc::sim::NO_RELIGION) {
        city.religion().pressure[faith] = pressure;
    }
    return city;
}

} // namespace

TEST_CASE("religion no longer switches off from the Renaissance on") {
    // It returned exactly 0.0f from era 3, which killed the whole
    // devotion-to-loyalty path for most of a long game.
    CHECK(aoc::sim::religionLoyaltyCoefficient(aoc::EraId{0}) > 0.0f);
    CHECK(aoc::sim::religionLoyaltyCoefficient(aoc::EraId{2}) > 0.0f);
    CHECK(aoc::sim::religionLoyaltyCoefficient(aoc::EraId{3}) > 0.0f);
    CHECK(aoc::sim::religionLoyaltyCoefficient(aoc::EraId{7}) > 0.0f);

    // Secular institutions taking over is a reason to weaken, not to vanish.
    CHECK(aoc::sim::religionLoyaltyCoefficient(aoc::EraId{3}) <
          aoc::sim::religionLoyaltyCoefficient(aoc::EraId{0}));
}

TEST_CASE("a city sharing its owner's faith is held; one following a rival's is not") {
    aoc::test::World w            = aoc::test::makeWorld(2);
    aoc::game::Player& owner      = *w.gameState.player(PlayerId{0});
    owner.faith().foundedReligion = OURS;

    aoc::game::City& city = cityFollowing(w, OURS, 100.0f);
    CHECK(aoc::sim::religionLoyaltyAlignment(city, owner) > 0.0f);

    // The same city, converted to a rival's faith, pulls the other way.
    city.religion().pressure[OURS]   = 0.0f;
    city.religion().pressure[THEIRS] = 100.0f;
    CHECK(aoc::sim::religionLoyaltyAlignment(city, owner) < 0.0f);
}

TEST_CASE("a city following nothing pulls neither way") {
    aoc::test::World w            = aoc::test::makeWorld(2);
    aoc::game::Player& owner      = *w.gameState.player(PlayerId{0});
    owner.faith().foundedReligion = OURS;
    aoc::game::City& city         = cityFollowing(w, aoc::sim::NO_RELIGION, 0.0f);
    CHECK(aoc::sim::religionLoyaltyAlignment(city, owner) == doctest::Approx(0.0f));
}

TEST_CASE("a faith is a rival's even when its owner has none of their own") {
    // An occupier with no religion still faces an institution holding its
    // citizens' allegiance.
    aoc::test::World w            = aoc::test::makeWorld(2);
    aoc::game::Player& owner      = *w.gameState.player(PlayerId{0});
    owner.faith().foundedReligion = aoc::sim::NO_RELIGION;
    aoc::game::City& city         = cityFollowing(w, THEIRS, 100.0f);
    CHECK(aoc::sim::religionLoyaltyAlignment(city, owner) < 0.0f);
}

TEST_CASE("the loyalty contribution is bounded in both directions") {
    // Religion tilts the balance; it does not settle it. An unbounded devout
    // city could out-weigh distance, amenities and the captured-city penalty
    // put together, and an empire could hold anything at any range by
    // converting it.
    CHECK(RELIGION_LOYALTY_LIMIT > 0.0f);

    aoc::test::World w            = aoc::test::makeWorld(2);
    aoc::game::Player& owner      = *w.gameState.player(PlayerId{0});
    owner.faith().foundedReligion = OURS;
    // Absurd devotion, far past anything the game produces.
    aoc::game::City& city = cityFollowing(w, OURS, 100000.0f);

    aoc::sim::computeCityLoyalty(w.gameState, w.grid, PlayerId{0});
    CHECK(city.loyalty().devotionBonus <= RELIGION_LOYALTY_LIMIT);
    CHECK(city.loyalty().devotionBonus >= -RELIGION_LOYALTY_LIMIT);
}

TEST_CASE("sharing a faith beats following a rival's, through the real pass") {
    // Two identical cities but for their religion, both scored by
    // computeCityLoyalty rather than the helper alone.
    const auto devotionOf = [](aoc::sim::ReligionId cityFaith) {
        aoc::test::World w            = aoc::test::makeWorld(2);
        aoc::game::Player& owner      = *w.gameState.player(PlayerId{0});
        owner.faith().foundedReligion = OURS;
        aoc::game::City& city         = cityFollowing(w, cityFaith, 60.0f);
        aoc::sim::computeCityLoyalty(w.gameState, w.grid, PlayerId{0});
        return city.loyalty().devotionBonus;
    };

    const float shared = devotionOf(OURS);
    const float rival  = devotionOf(THEIRS);
    CHECK(shared > rival);
    CHECK(rival <= 0.0f); // a rival's church is never an asset
}
