/**
 * @file test_historic_moments.cpp
 * @brief Historic Moments: every era-score award is recorded with its turn, points
 *        and text, the timeline keeps the newest 64, the lifetime score survives the
 *        age reset, an age change is recorded, and recruiting a great person is a
 *        moment. Until 2026-09-05 awards were only logged.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/greatpeople/GreatPeople.hpp"
#include "aoc/simulation/tech/EraScore.hpp"

using aoc::PlayerId;
using aoc::sim::AgeType;
using aoc::sim::MAX_HISTORIC_MOMENTS;

TEST_CASE("an award records a moment and feeds both the era score and the lifetime score") {
    aoc::test::World w   = aoc::test::makeWorld(2);
    aoc::game::Player& p = *w.gameState.player(PlayerId{0});
    aoc::sim::addEraScore(p, 12, 3, "Completed the Pyramids");
    aoc::sim::addEraScore(p, 15, 2, "Researched Mining");
    REQUIRE(p.eraScore().moments.size() == 2);
    CHECK(p.eraScore().moments[0].turn == 12);
    CHECK(p.eraScore().moments[0].points == 3);
    CHECK(p.eraScore().moments[0].text == "Completed the Pyramids");
    CHECK(p.eraScore().moments[1].text == "Researched Mining");
    CHECK(p.eraScore().eraScore == 5);
    CHECK(p.eraScore().lifetimeEraScore == 5);
    CHECK(aoc::sim::ageTypeName(AgeType::Golden) == "Golden");
}

TEST_CASE("the timeline keeps the newest 64 moments") {
    aoc::sim::PlayerEraScoreComponent score;
    for (int32_t i = 0; i < 70; ++i) {
        aoc::sim::recordHistoricMoment(score, i, 1, "Moment " + std::to_string(i));
    }
    REQUIRE(score.moments.size() == MAX_HISTORIC_MOMENTS);
    CHECK(score.moments.front().turn == 6); // the six oldest were dropped
    CHECK(score.moments.back().turn == 69);
}

TEST_CASE("the age transition records the new age and resets the score, not the lifetime") {
    aoc::test::World w                       = aoc::test::makeWorld(2);
    aoc::game::Player& p                     = *w.gameState.player(PlayerId{0});
    aoc::sim::PlayerEraScoreComponent& score = p.eraScore();
    aoc::sim::addEraScore(p, 5, score.goldenAgeThreshold + 1, "Everything at once");
    aoc::sim::checkEraTransition(p, 10);
    CHECK(score.currentAgeType == AgeType::Golden);
    CHECK(score.turnsRemaining == 10);
    CHECK(score.eraScore <= 3); // reset (Taj Mahal would bank 3)
    CHECK(score.lifetimeEraScore == score.goldenAgeThreshold + 1 - 0);
    REQUIRE(score.moments.size() == 2);
    CHECK(score.moments.back().text == "Entered a Golden Age");
    CHECK(score.moments.back().points == 0);
    CHECK(score.moments.back().turn == 10);

    aoc::sim::checkEraTransition(p, 20); // nothing earned since: below the dark threshold
    CHECK(score.currentAgeType == AgeType::Dark);
    CHECK(score.moments.back().text == "Fell into a Dark Age");
    CHECK(score.moments.back().turn == 20);
}

TEST_CASE("recruiting a great person is a historic moment") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Home");
    aoc::game::Player& p = *w.gameState.players()[0];
    w.gameState.setCurrentTurn(33);
    p.greatPeople().points[static_cast<uint8_t>(aoc::sim::GreatPersonType::General)] =
        p.greatPeople().threshold(aoc::sim::GreatPersonType::General) + 1.0f;
    aoc::sim::checkGreatPeopleRecruitment(w.gameState, PlayerId{0});
    REQUIRE(!p.eraScore().moments.empty());
    CHECK(p.eraScore().moments.back().turn == 33);
    CHECK(p.eraScore().moments.back().points == 2);
    CHECK(p.eraScore().moments.back().text.rfind("Recruited ", 0) == 0);
    CHECK(p.eraScore().eraScore == 2);
}
