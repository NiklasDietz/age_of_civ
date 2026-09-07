/**
 * @file test_belief_effects.cpp
 * @brief Beliefs change the game, not just the religion screen.
 *
 * `BeliefDef` carried gold and science per follower city, an amenity bonus, a
 * food bonus and a spread multiplier. Every one of those fields was written by
 * the belief table, printed by the religion screen, and read by nothing in the
 * simulation, so choosing a belief was cosmetic.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/city/Happiness.hpp"
#include "aoc/simulation/religion/Religion.hpp"

using aoc::PlayerId;

namespace {

/// Index of the first belief of `type` that actually carries `field`.
template <typename Pick>
int32_t beliefWith(aoc::sim::BeliefType type, Pick pick) {
    const std::array<aoc::sim::BeliefDef, aoc::sim::BELIEF_COUNT>& all = aoc::sim::allBeliefs();
    for (uint8_t i = 0; i < aoc::sim::BELIEF_COUNT; ++i) {
        if (all[i].type == type && pick(all[i]) > 0.0f) { return i; }
    }
    return -1;
}

} // namespace

TEST_CASE("a follower belief with an amenity bonus reaches the city that holds the faith") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::game::City& city = aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Home");
    aoc::game::Player& p  = *w.gameState.player(PlayerId{0});

    const int32_t belief =
        beliefWith(aoc::sim::BeliefType::Follower,
                   [](const aoc::sim::BeliefDef& b) { return b.amenityBonus; });
    REQUIRE(belief >= 0);

    const aoc::sim::ReligionId faith =
        w.gameState.religionTracker().foundReligion("Testament", PlayerId{0});
    w.gameState.religionTracker().religions[faith].followerBelief =
        static_cast<uint8_t>(belief);
    city.religion().addPressure(faith, 500.0f);
    REQUIRE(city.religion().dominantReligion() == faith);

    aoc::sim::computeCityHappiness(p, nullptr);
    const float withoutTracker = city.happiness().amenities;

    aoc::sim::computeCityHappiness(p, &w.gameState.religionTracker());
    const float withTracker = city.happiness().amenities;

    CHECK(withTracker > withoutTracker); // the belief actually landed
}

TEST_CASE("a founder belief pays its founder for the cities that follow") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Home");
    aoc::game::City& follower = aoc::test::addCityAt(w, PlayerId{1}, 15, 15, "Abroad");
    aoc::game::Player& founder = *w.gameState.player(PlayerId{0});

    const int32_t goldBelief =
        beliefWith(aoc::sim::BeliefType::Founder,
                   [](const aoc::sim::BeliefDef& b) { return b.goldPerFollowerCity; });
    REQUIRE(goldBelief >= 0);

    const aoc::sim::ReligionId faith =
        w.gameState.religionTracker().foundReligion("Testament", PlayerId{0});
    w.gameState.religionTracker().religions[faith].founderBelief =
        static_cast<uint8_t>(goldBelief);

    const int64_t before = founder.monetary().treasury;
    aoc::sim::processFounderBeliefs(w.gameState);
    CHECK(founder.monetary().treasury == before); // nobody follows it yet

    // A rival's city adopting the faith pays the founder, which is the point.
    follower.religion().addPressure(faith, 500.0f);
    REQUIRE(follower.religion().dominantReligion() == faith);
    aoc::sim::processFounderBeliefs(w.gameState);
    CHECK(founder.monetary().treasury > before);
}

TEST_CASE("every belief in the table carries an effect the simulation can read") {
    // A belief with every field at zero is a name on a screen. This is what
    // stops one being added that way.
    const std::array<aoc::sim::BeliefDef, aoc::sim::BELIEF_COUNT>& all = aoc::sim::allBeliefs();
    for (uint8_t i = 0; i < aoc::sim::BELIEF_COUNT; ++i) {
        const aoc::sim::BeliefDef& b = all[i];
        const float total = b.goldPerFollowerCity + b.sciencePerFollowerCity + b.amenityBonus
                            + b.foodBonus + b.faithBonus + b.spreadStrength;
        INFO("belief ", i, " ", std::string(b.name));
        CHECK(total > 0.0f);
    }
}

TEST_CASE("a religion takes a seat, and a faith nobody tends fades") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::game::City& home = aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Home");
    aoc::game::City& away = aoc::test::addCityAt(w, PlayerId{1}, 15, 15, "Away");
    aoc::game::Player& p  = *w.gameState.player(PlayerId{0});
    p.faith().hasPantheon = true;
    p.faith().faith       = 1000.0f;

    const aoc::sim::ReligionId faith = aoc::sim::foundReligionFor(w.gameState, PlayerId{0});
    REQUIRE(faith != aoc::sim::NO_RELIGION);

    SUBCASE("the founding city becomes the holy city") {
        const aoc::sim::ReligionDef& def = w.gameState.religionTracker().religions[faith];
        CHECK(def.hasHolyCity);
        CHECK(def.holyCity == home.location());
    }

    SUBCASE("the holy city renews its own faith each turn") {
        const float before = home.religion().pressure[faith];
        aoc::sim::processHolyCityAndDecay(w.gameState);
        CHECK(home.religion().pressure[faith] > before); // the seat gains
    }

    SUBCASE("pressure elsewhere fades when nothing feeds it") {
        away.religion().addPressure(faith, 100.0f);
        const float before = away.religion().pressure[faith];
        aoc::sim::processHolyCityAndDecay(w.gameState);
        CHECK(away.religion().pressure[faith] < before); // no seat, no renewal
    }

    SUBCASE("a trace of faith is dropped rather than lingering forever") {
        away.religion().addPressure(faith, aoc::sim::PRESSURE_FLOOR * 0.5f);
        aoc::sim::processHolyCityAndDecay(w.gameState);
        CHECK(away.religion().pressure[faith] == doctest::Approx(0.0f));
    }
}
