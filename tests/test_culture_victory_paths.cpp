/**
 * @file test_culture_victory_paths.cpp
 * @brief Culture victory is evaluated twice, by two paths that disagree.
 *
 *   Path A: `computeTourism` + `checkCulturalVictory` (culture/Tourism.cpp).
 *           Tourism = 3*wonders + 2*great-work slots + 2*holy sites, scaled by
 *           trade agreements and diplomatic resistance. A "winner" must hold
 *           >= 500 foreign tourists AND beat EVERY rival's domestic count.
 *           The turn loop only awards that winner +5 era points per turn.
 *   Path B: `checkVictoryConditions` (victory/VictoryCondition.cpp). Its own
 *           tourism (the per-player accumulator: works, wonders, holy sites) feeds
 *           totalCultureAccumulated; the win needs the culture threshold, 3
 *           wonders, a 1.25x lead, and foreign tourists beating at least HALF
 *           the rivals. This one ends the game.
 *
 * These cases pin the CURRENT behaviour of both so a later consolidation
 * (Phase 1 item 3) changes it on purpose, not by accident.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/culture/Tourism.hpp"
#include "aoc/simulation/victory/VictoryCondition.hpp"
#include "aoc/simulation/wonder/Wonder.hpp"

using aoc::PlayerId;
using aoc::sim::DistrictType;
using aoc::sim::VictoryResult;
using aoc::sim::VictoryType;

namespace {

constexpr aoc::BuildingId SHRINE{36};
constexpr int32_t WINDOW_TURN = 400;  ///< past 70% of a 500-turn game
constexpr int32_t MAX_TURNS   = 500;

/// A holy-site district with a Shrine, which is what path A counts as a holy site.
void addHolySite(aoc::game::City& city, int32_t q, int32_t r) {
    aoc::sim::CityDistrictsComponent::PlacedDistrict d{};
    d.type     = DistrictType::HolySite;
    d.location = {q, r};
    d.buildings.push_back(SHRINE);
    city.districts().districts.push_back(d);
}

} // namespace

TEST_CASE("path A: tourism is 3 per wonder plus 2 per holy site, tourists derive from it") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::game::City& capital = aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Home");
    capital.wonders().wonders.push_back(aoc::sim::WonderId{1});
    addHolySite(capital, 6, 5);

    aoc::sim::computeTourism(w.gameState, PlayerId{0}, w.grid, nullptr);
    const aoc::sim::PlayerTourismComponent& t = w.gameState.player(PlayerId{0})->tourism();
    CHECK(t.wonderCount == 1);
    CHECK(t.tourismPerTurn == doctest::Approx(5.0f));   // 3*1 + 2*0 + 2*1
    CHECK(t.cumulativeTourism == doctest::Approx(5.0f));
    CHECK(t.foreignTourists == 0);                      // 150 tourism per foreign tourist

    for (int32_t i = 0; i < 29; ++i) {
        aoc::sim::computeTourism(w.gameState, PlayerId{0}, w.grid, nullptr);
    }
    CHECK(t.cumulativeTourism == doctest::Approx(150.0f));
    CHECK(t.foreignTourists == 1);

    w.gameState.player(PlayerId{0})->victoryTracker().totalCultureAccumulated = 250.0f;
    aoc::sim::computeTourism(w.gameState, PlayerId{0}, w.grid, nullptr);
    CHECK(t.domesticTourists == 2);                     // 100 culture per domestic tourist
}

TEST_CASE("path A: a winner needs 500 foreign tourists and must beat every rival") {
    aoc::test::World w = aoc::test::makeWorld(3);
    aoc::game::Player& p0 = *w.gameState.players()[0];
    aoc::game::Player& p1 = *w.gameState.players()[1];
    aoc::game::Player& p2 = *w.gameState.players()[2];

    p0.tourism().foreignTourists = 400;
    p1.tourism().domesticTourists = 100;
    p2.tourism().domesticTourists = 100;
    CHECK(aoc::sim::checkCulturalVictory(w.gameState) == aoc::INVALID_PLAYER);  // below the floor

    p0.tourism().foreignTourists = 600;
    CHECK(aoc::sim::checkCulturalVictory(w.gameState) == PlayerId{0});

    p1.tourism().domesticTourists = 700;  // one rival resists
    CHECK(aoc::sim::checkCulturalVictory(w.gameState) == aoc::INVALID_PLAYER);
}

TEST_CASE("the two paths disagree: B ends the game on a majority, A demands a sweep") {
    aoc::test::World w = aoc::test::makeWorld(3);
    aoc::game::City& capital = aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Home");
    aoc::test::addCityAt(w, PlayerId{1}, 12, 9, "Thebes");
    aoc::test::addCityAt(w, PlayerId{2}, 18, 4, "Ur");
    for (uint8_t i = 1; i <= 3; ++i) {
        capital.wonders().wonders.push_back(aoc::sim::WonderId{i});
    }
    aoc::game::Player& p0 = *w.gameState.players()[0];
    aoc::game::Player& p1 = *w.gameState.players()[1];
    aoc::game::Player& p2 = *w.gameState.players()[2];
    p0.victoryTracker().totalCultureAccumulated = 1.0e7f;  // clears any paced threshold
    p0.tourism().foreignTourists  = 600;
    p1.tourism().domesticTourists = 700;  // this rival resists
    p2.tourism().domesticTourists = 100;

    // Path B: 1 of 2 rivals beaten is "at least half", so it declares the win.
    const VictoryResult b = aoc::sim::checkVictoryConditions(
        w.gameState, WINDOW_TURN, MAX_TURNS, aoc::sim::VICTORY_MASK_CULTURE, nullptr);
    CHECK(b.type == VictoryType::Culture);
    CHECK(b.winner == PlayerId{0});

    // Path A on the same state: the resisting rival blocks it.
    CHECK(aoc::sim::checkCulturalVictory(w.gameState) == aoc::INVALID_PLAYER);

    // Remove the resistance and both agree.
    p1.tourism().domesticTourists = 100;
    CHECK(aoc::sim::checkCulturalVictory(w.gameState) == PlayerId{0});
    CHECK(aoc::sim::checkVictoryConditions(w.gameState, WINDOW_TURN, MAX_TURNS,
                                           aoc::sim::VICTORY_MASK_CULTURE, nullptr)
              .winner == PlayerId{0});
}

TEST_CASE("path B: before the victory window nothing wins, whatever the numbers say") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::game::City& capital = aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Home");
    aoc::test::addCityAt(w, PlayerId{1}, 12, 9, "Thebes");
    for (uint8_t i = 1; i <= 3; ++i) {
        capital.wonders().wonders.push_back(aoc::sim::WonderId{i});
    }
    w.gameState.players()[0]->victoryTracker().totalCultureAccumulated = 1.0e7f;
    w.gameState.players()[0]->tourism().foreignTourists = 600;

    const VictoryResult early = aoc::sim::checkVictoryConditions(
        w.gameState, 100, MAX_TURNS, aoc::sim::VICTORY_MASK_CULTURE, nullptr);
    CHECK(early.type == VictoryType::None);
    const VictoryResult late = aoc::sim::checkVictoryConditions(
        w.gameState, WINDOW_TURN, MAX_TURNS, aoc::sim::VICTORY_MASK_CULTURE, nullptr);
    CHECK(late.type == VictoryType::Culture);
}
