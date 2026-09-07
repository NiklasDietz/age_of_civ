/**
 * @file test_builder_retire.cpp
 * @brief A builder that spends its last charge is removed by the request layer
 *        itself. Until 2026-09-07 the four builder requests spent the charge but
 *        left the unit standing; removal lived in the HUD, the game-control
 *        dispatcher and the AI separately, so every caller had to remember, and
 *        any new one -- a headless request path, a script -- would leak
 *        0-charge builders onto the map.
 *
 *        Driven through the requests, never through the UI.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/simulation/unit/BuilderActions.hpp"
#include "aoc/simulation/unit/UnitOrders.hpp"

using aoc::ErrorCode;
using aoc::PlayerId;
using aoc::UnitTypeId;

namespace {

constexpr UnitTypeId BUILDER{5};
constexpr aoc::hex::AxialCoord AT{5, 5};

/// A builder for player 0 at AT with `charges` charges, plus the Mining tech a
/// chop needs and a city near enough to receive a chop's production.
aoc::game::Unit& builderWith(aoc::test::World& w, int32_t charges) {
    aoc::game::Unit& b = aoc::test::addUnitAt(w, PlayerId{0}, BUILDER, AT.q, AT.r);
    b.setChargesRemaining(charges);
    return b;
}

} // namespace

TEST_CASE("a one-charge builder is gone after requestPlaceImprovement") {
    aoc::test::World w = aoc::test::makeWorld(1);
    builderWith(w, 1);
    aoc::game::Player& player = *w.gameState.player(PlayerId{0});
    REQUIRE(player.units().size() == 1);
    REQUIRE(player.unitAt(AT) != nullptr);

    // A Farm on grassland needs no tech, so the request turns on the charge.
    const ErrorCode rc = aoc::sim::requestPlaceImprovement(w.gameState, w.grid, PlayerId{0}, AT,
                                                           aoc::map::ImprovementType::Farm);
    REQUIRE(rc == ErrorCode::Ok);

    // The request retired it: no unit left, and nothing standing on the tile.
    CHECK(player.units().empty());
    CHECK(player.unitAt(AT) == nullptr);
}

TEST_CASE("a two-charge builder survives the first improvement and goes on the second") {
    aoc::test::World w = aoc::test::makeWorld(1);
    builderWith(w, 2);
    aoc::game::Player& player = *w.gameState.player(PlayerId{0});

    REQUIRE(aoc::sim::requestPlaceImprovement(w.gameState, w.grid, PlayerId{0}, AT,
                                              aoc::map::ImprovementType::Farm) == ErrorCode::Ok);
    // One charge spent, one left: still on the map.
    REQUIRE(player.unitAt(AT) != nullptr);
    CHECK(player.unitAt(AT)->chargesRemaining() == 1);

    // The tile already has a Farm, so improve a different one. Move it first.
    constexpr aoc::hex::AxialCoord NEXT{6, 5};
    player.unitAt(AT)->setPosition(NEXT);
    REQUIRE(aoc::sim::requestPlaceImprovement(w.gameState, w.grid, PlayerId{0}, NEXT,
                                              aoc::map::ImprovementType::Farm) == ErrorCode::Ok);
    CHECK(player.units().empty());
}

TEST_CASE("a one-charge builder is gone after requestRepair") {
    aoc::test::World w = aoc::test::makeWorld(1);
    builderWith(w, 1);
    aoc::game::Player& player = *w.gameState.player(PlayerId{0});

    // requestRepair needs an owned, pillaged tile under the builder.
    const int32_t idx = w.grid.toIndex(AT);
    w.grid.setOwner(idx, PlayerId{0});
    w.grid.setImprovement(idx, aoc::map::ImprovementType::Farm);
    w.grid.setPillaged(idx, true);

    REQUIRE(aoc::sim::requestRepair(w.gameState, w.grid, PlayerId{0}, AT) == ErrorCode::Ok);
    CHECK(player.units().empty());
    CHECK_FALSE(w.grid.isPillaged(idx)); // and the work was actually done
}

TEST_CASE("a rejected request neither spends a charge nor retires the builder") {
    aoc::test::World w = aoc::test::makeWorld(1);
    builderWith(w, 1);
    aoc::game::Player& player = *w.gameState.player(PlayerId{0});

    // Nothing pillaged here, so the repair is refused.
    const ErrorCode rc = aoc::sim::requestRepair(w.gameState, w.grid, PlayerId{0}, AT);
    CHECK(rc != ErrorCode::Ok);

    REQUIRE(player.unitAt(AT) != nullptr);
    CHECK(player.unitAt(AT)->chargesRemaining() == 1); // charge untouched
}
