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
#include "aoc/balance/BalanceParams.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/religion/Religion.hpp"

using aoc::PlayerId;
using aoc::sim::RELIGION_LOYALTY_LIMIT;

namespace {

constexpr aoc::sim::ReligionId OURS{0};
constexpr aoc::sim::ReligionId THEIRS{1};

/// Register OURS to player 0 and THEIRS to player 1, so each faith has a
/// patron. A faith nobody founded has nobody holding the other end of the
/// wedge, and is now inert by design.
void foundBothFaiths(aoc::test::World& w) {
    REQUIRE(w.gameState.religionTracker().foundReligion("Ours", PlayerId{0}) == OURS);
    REQUIRE(w.gameState.religionTracker().foundReligion("Theirs", PlayerId{1}) == THEIRS);
}

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
    aoc::test::World w = aoc::test::makeWorld(2);
    foundBothFaiths(w);
    aoc::game::Player& owner      = *w.gameState.player(PlayerId{0});
    owner.faith().foundedReligion = OURS;

    aoc::game::City& city  = cityFollowing(w, OURS, 100.0f);
    const float shared     = aoc::sim::religionLoyaltyAlignment(city, owner, w.gameState);
    CHECK(shared > 0.0f);

    // The same city, converted to a rival's faith, pulls the other way.
    city.religion().pressure[OURS]   = 0.0f;
    city.religion().pressure[THEIRS] = 100.0f;
    const float rival = aoc::sim::religionLoyaltyAlignment(city, owner, w.gameState);
    CHECK(rival < 0.0f);

    // But not as hard as its own church holds it. The owner still governs,
    // taxes and garrisons the city either way; the rival patron has the
    // citizens' allegiance and nothing else. Symmetric, this term decided the
    // game -- most cities follow a faith their owner did not found.
    CHECK(-rival < shared);
}

TEST_CASE("a city following nothing pulls neither way") {
    aoc::test::World w            = aoc::test::makeWorld(2);
    aoc::game::Player& owner      = *w.gameState.player(PlayerId{0});
    owner.faith().foundedReligion = OURS;
    aoc::game::City& city         = cityFollowing(w, aoc::sim::NO_RELIGION, 0.0f);
    CHECK(aoc::sim::religionLoyaltyAlignment(city, owner, w.gameState) == doctest::Approx(0.0f));
}

TEST_CASE("a faith is a rival's even when its owner has none of their own") {
    // An occupier with no religion still faces an institution holding its
    // citizens' allegiance.
    aoc::test::World w = aoc::test::makeWorld(2);
    foundBothFaiths(w);
    aoc::game::Player& owner      = *w.gameState.player(PlayerId{0});
    owner.faith().foundedReligion = aoc::sim::NO_RELIGION;
    aoc::game::City& city         = cityFollowing(w, THEIRS, 100.0f);
    CHECK(aoc::sim::religionLoyaltyAlignment(city, owner, w.gameState) < 0.0f);
}

TEST_CASE("a faith with no patron left pulls at nothing") {
    // The wedge needs somebody holding the other end. Counting every faith the
    // owner did not found as a hostile institution meant nearly every city on
    // the map paid the penalty simultaneously -- a global loyalty drain rather
    // than a religious contest, which flipped 39 cities to Free City on seed 43
    // and let barbarians eliminate three of four civs.
    aoc::test::World w = aoc::test::makeWorld(2);
    foundBothFaiths(w);
    aoc::game::Player& owner      = *w.gameState.player(PlayerId{0});
    owner.faith().foundedReligion = OURS;
    aoc::game::City& city         = cityFollowing(w, THEIRS, 100.0f);
    REQUIRE(aoc::sim::religionLoyaltyAlignment(city, owner, w.gameState) < 0.0f);

    // Its patron is knocked out of the game: the church remains, the pull does
    // not.
    w.gameState.player(PlayerId{1})->victoryTracker().isEliminated = true;
    CHECK(aoc::sim::religionLoyaltyAlignment(city, owner, w.gameState) ==
          doctest::Approx(0.0f));
}

TEST_CASE("an unfounded faith is not a rival's church") {
    // Nothing registered THEIRS, so no player is its patron.
    aoc::test::World w            = aoc::test::makeWorld(2);
    aoc::game::Player& owner      = *w.gameState.player(PlayerId{0});
    owner.faith().foundedReligion = aoc::sim::NO_RELIGION;
    aoc::game::City& city         = cityFollowing(w, THEIRS, 100.0f);
    CHECK(aoc::sim::religionLoyaltyAlignment(city, owner, w.gameState) ==
          doctest::Approx(0.0f));
}

TEST_CASE("the loyalty contribution is bounded in both directions") {
    // Religion tilts the balance; it does not settle it. An unbounded devout
    // city could out-weigh distance, amenities and the captured-city penalty
    // put together, and an empire could hold anything at any range by
    // converting it.
    CHECK(RELIGION_LOYALTY_LIMIT > 0.0f);
    // And it stays under the captured-city penalty, which is meant to be the
    // harshest single term in the loyalty sum.
    CHECK(RELIGION_LOYALTY_LIMIT < 8.0f);

    aoc::test::World w = aoc::test::makeWorld(2);
    foundBothFaiths(w);
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
        aoc::test::World w = aoc::test::makeWorld(2);
        foundBothFaiths(w);
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

TEST_CASE("dominating a rival means a majority of its cities, not a token presence") {
    // The bar was 0.08: a twelve-city rival counted as dominated when ONE city
    // converted, so the religious victory measured presence, not domination. It
    // had been eased there to make the victory fire at all, and sat OUTSIDE its
    // own GA search bounds of [0.3, 0.8] -- the tuner could never explore the
    // shipped value, and any tuned genome jumped it to at least 0.3.
    const aoc::balance::BalanceParams& bal = aoc::balance::params();
    CHECK(bal.religionDominanceFrac >= 0.5f);

    // And it must sit inside the range the tuner is allowed to search, or the
    // default and the design intent disagree again.
    const aoc::balance::BalanceBounds bounds = aoc::balance::defaultBalanceBounds();
    bool inRange = false;
    for (int32_t i = 0; i < aoc::balance::BALANCE_PARAM_COUNT; ++i) {
        const std::size_t idx = static_cast<std::size_t>(i);
        if (bal.religionDominanceFrac >= bounds.min[idx] &&
            bal.religionDominanceFrac <= bounds.max[idx] &&
            bounds.min[idx] >= 0.3f && bounds.max[idx] <= 0.8f) {
            inRange = true;
        }
    }
    CHECK(inRange);
}

TEST_CASE("an Inquisitor purges rival faiths from its own city") {
    // This path lived in a dead second implementation of theological combat and
    // had no caller anywhere. It matters now: a rival's faith in your city
    // costs you loyalty, so clearing it is a real act.
    aoc::test::World w            = aoc::test::makeWorld(2);
    aoc::game::Player& owner      = *w.gameState.player(PlayerId{0});
    owner.faith().foundedReligion = OURS;

    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha");
    aoc::game::City& city              = *owner.cities()[0];
    city.religion().pressure[OURS]     = 40.0f;
    city.religion().pressure[THEIRS]   = 90.0f;
    REQUIRE(city.religion().dominantReligion() == THEIRS);

    aoc::game::Unit& inq = owner.addUnit(aoc::sim::INQUISITOR_UNIT_ID, {5, 5});
    inq.setChargesRemaining(2);

    REQUIRE(aoc::sim::requestPurgeReligion(w.gameState, PlayerId{0}, {5, 5}) == aoc::ErrorCode::Ok);

    // The rival faith is gone; ours is untouched.
    CHECK(city.religion().pressure[THEIRS] == doctest::Approx(0.0f));
    CHECK(city.religion().pressure[OURS] == doctest::Approx(40.0f));
    CHECK(city.religion().dominantReligion() == OURS);
    // And it cost a charge.
    CHECK(inq.chargesRemaining() == 1);
}

TEST_CASE("a spent Inquisitor is consumed by the purge") {
    aoc::test::World w            = aoc::test::makeWorld(2);
    aoc::game::Player& owner      = *w.gameState.player(PlayerId{0});
    owner.faith().foundedReligion = OURS;
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha");
    owner.cities()[0]->religion().pressure[THEIRS] = 50.0f;

    aoc::game::Unit& inq = owner.addUnit(aoc::sim::INQUISITOR_UNIT_ID, {5, 5});
    inq.setChargesRemaining(1);
    REQUIRE(owner.units().size() == 1);

    REQUIRE(aoc::sim::requestPurgeReligion(w.gameState, PlayerId{0}, {5, 5}) == aoc::ErrorCode::Ok);
    CHECK(owner.units().empty());
}

TEST_CASE("there is nothing to purge from a city of one's own faith") {
    aoc::test::World w            = aoc::test::makeWorld(2);
    aoc::game::Player& owner      = *w.gameState.player(PlayerId{0});
    owner.faith().foundedReligion = OURS;
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha");
    owner.cities()[0]->religion().pressure[OURS] = 50.0f;

    aoc::game::Unit& inq = owner.addUnit(aoc::sim::INQUISITOR_UNIT_ID, {5, 5});
    inq.setChargesRemaining(1);

    CHECK(aoc::sim::requestPurgeReligion(w.gameState, PlayerId{0}, {5, 5}) != aoc::ErrorCode::Ok);
    CHECK(inq.chargesRemaining() == 1); // no charge wasted
}

TEST_CASE("an Inquisitor cannot purge another civ's city") {
    aoc::test::World w            = aoc::test::makeWorld(2);
    aoc::game::Player& owner      = *w.gameState.player(PlayerId{0});
    owner.faith().foundedReligion = OURS;
    aoc::test::addCityAt(w, PlayerId{1}, 9, 9, "Theirs");
    w.gameState.player(PlayerId{1})->cities()[0]->religion().pressure[THEIRS] = 50.0f;

    aoc::game::Unit& inq = owner.addUnit(aoc::sim::INQUISITOR_UNIT_ID, {9, 9});
    inq.setChargesRemaining(1);

    CHECK(aoc::sim::requestPurgeReligion(w.gameState, PlayerId{0}, {9, 9}) != aoc::ErrorCode::Ok);
}
