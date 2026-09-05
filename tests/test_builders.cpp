/**
 * @file test_builders.cpp
 * @brief Builder orders through the shared requests: the picker lists only
 *        what the unit may place, placing spends a charge, chopping needs the
 *        tech and pays the nearest building city, harvesting removes a bonus
 *        resource and feeds the city, and the Military Engineer alone lays
 *        roads, railways and forts. Civ VI plan Phase 2.4, 2026-09-05.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/unit/BuilderActions.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"

#include <algorithm>
#include <vector>

using aoc::ErrorCode;
using aoc::PlayerId;
using aoc::UnitTypeId;
using aoc::hex::AxialCoord;
using aoc::map::FeatureType;
using aoc::map::ImprovementType;

namespace {

constexpr UnitTypeId BUILDER{5};
constexpr UnitTypeId ENGINEER{64};

bool contains(const std::vector<ImprovementType>& v, ImprovementType t) {
    return std::find(v.begin(), v.end(), t) != v.end();
}

} // namespace

TEST_CASE("the picker lists terrain-legal, tech-known improvements; placing spends a charge") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::game::Player& p = *w.gameState.player(PlayerId{0});
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Home");
    const AxialCoord at{6, 5};
    const int32_t idx = w.grid.toIndex(at);
    w.grid.setOwner(idx, PlayerId{0});
    w.grid.setTerrain(idx, aoc::map::TerrainType::Grassland);
    w.grid.setFeature(idx, FeatureType::None);
    aoc::game::Unit& builder = aoc::test::addUnitAt(w, PlayerId{0}, BUILDER, at.q, at.r);

    std::vector<ImprovementType> options = aoc::sim::placeableImprovements(w.grid, idx, builder, p.tech());
    CHECK(contains(options, ImprovementType::Farm));
    CHECK_FALSE(contains(options, ImprovementType::Road));      // engineers only
    CHECK_FALSE(contains(options, ImprovementType::Mine));      // grassland, no hills
    CHECK(aoc::sim::requestPlaceImprovement(w.gameState, w.grid, PlayerId{0}, at, ImprovementType::Road)
          == ErrorCode::InvalidUnitAction);
    CHECK(aoc::sim::requestPlaceImprovement(w.gameState, w.grid, PlayerId{0}, at, ImprovementType::Farm)
          == ErrorCode::Ok);
    CHECK(w.grid.improvement(idx) == ImprovementType::Farm);
    CHECK(builder.chargesRemaining() == 2);
    // The tile is taken now.
    CHECK(aoc::sim::placeableImprovements(w.grid, idx, builder, p.tech()).empty());
    CHECK(aoc::sim::requestPlaceImprovement(w.gameState, w.grid, PlayerId{1}, at, ImprovementType::Farm)
          == ErrorCode::InvalidArgument);   // not that player's unit
}

TEST_CASE("chopping needs the tech, removes the feature and pays the nearest building city") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::game::Player& p = *w.gameState.player(PlayerId{0});
    aoc::game::City& city = aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Home");
    const AxialCoord at{6, 5};
    const int32_t idx = w.grid.toIndex(at);
    w.grid.setOwner(idx, PlayerId{0});
    w.grid.setFeature(idx, FeatureType::Forest);
    aoc::game::Unit& builder = aoc::test::addUnitAt(w, PlayerId{0}, BUILDER, at.q, at.r);
    CHECK(aoc::sim::canChopAt(w.grid, idx));

    CHECK(aoc::sim::requestChop(w.gameState, w.grid, PlayerId{0}, at) == ErrorCode::TechPrerequisiteNotMet);
    p.tech().completedTechs[0] = true;   // Mining
    CHECK(aoc::sim::requestChop(w.gameState, w.grid, PlayerId{0}, at) == ErrorCode::InvalidState); // nothing queued
    aoc::sim::ProductionQueueItem item{};
    item.type      = aoc::sim::ProductionItemType::Building;
    item.itemId    = 16;
    item.name      = "Monument";
    item.totalCost = 200.0f;
    city.production().queue.push_back(item);
    CHECK(aoc::sim::requestChop(w.gameState, w.grid, PlayerId{0}, at) == ErrorCode::Ok);
    CHECK(w.grid.feature(idx) == FeatureType::None);
    CHECK(city.production().queue.front().progress == doctest::Approx(static_cast<float>(aoc::sim::builderYield(0))));
    CHECK(builder.chargesRemaining() == 2);
    CHECK(aoc::sim::builderYield(3) == 50);
    CHECK(aoc::sim::requestChop(w.gameState, w.grid, PlayerId{0}, at) == ErrorCode::InvalidArgument); // bare now
}

TEST_CASE("harvesting removes a bonus resource and feeds the nearest city") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::game::City& city = aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Home");
    const AxialCoord at{6, 5};
    const int32_t idx = w.grid.toIndex(at);
    w.grid.setOwner(idx, PlayerId{0});
    aoc::ResourceId bonus{};
    for (uint16_t g = 0; g < 200; ++g) {
        if (aoc::sim::goodDef(g).category == aoc::sim::GoodCategory::RawBonus) { bonus = aoc::ResourceId{g}; break; }
    }
    REQUIRE(bonus.isValid());
    w.grid.setResource(idx, bonus);
    aoc::test::addUnitAt(w, PlayerId{0}, BUILDER, at.q, at.r);
    CHECK(aoc::sim::canHarvestAt(w.grid, idx));
    const float before = city.foodSurplus();
    CHECK(aoc::sim::requestHarvest(w.gameState, w.grid, PlayerId{0}, at) == ErrorCode::Ok);
    CHECK_FALSE(w.grid.resource(idx).isValid());
    CHECK(city.foodSurplus() == doctest::Approx(before + static_cast<float>(aoc::sim::builderYield(0))));
    CHECK(aoc::sim::requestHarvest(w.gameState, w.grid, PlayerId{0}, at) == ErrorCode::InvalidArgument);
}

TEST_CASE("the Military Engineer lays roads, railways with Industrialization, and forts") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::game::Player& p = *w.gameState.player(PlayerId{0});
    const AxialCoord at{8, 8};
    const int32_t idx = w.grid.toIndex(at);
    w.grid.setOwner(idx, PlayerId{0});
    w.grid.setTerrain(idx, aoc::map::TerrainType::Grassland);
    w.grid.setFeature(idx, FeatureType::None);
    aoc::game::Unit& engineer = aoc::test::addUnitAt(w, PlayerId{0}, ENGINEER, at.q, at.r);
    CHECK(aoc::sim::isMilitaryEngineer(engineer));
    CHECK(engineer.chargesRemaining() == aoc::sim::MILITARY_ENGINEER_CHARGES);
    CHECK(aoc::sim::unitTypeDef(ENGINEER).requiredTech == aoc::TechId{43});

    std::vector<ImprovementType> options = aoc::sim::placeableImprovements(w.grid, idx, engineer, p.tech());
    CHECK(contains(options, ImprovementType::Road));
    CHECK(contains(options, ImprovementType::Fort));
    CHECK_FALSE(contains(options, ImprovementType::Railway));   // Industrialization missing
    CHECK_FALSE(contains(options, ImprovementType::Farm));      // builders only
    CHECK(aoc::sim::requestPlaceImprovement(w.gameState, w.grid, PlayerId{0}, at, ImprovementType::Road) == ErrorCode::Ok);
    p.tech().completedTechs[11] = true;   // Industrialization
    options = aoc::sim::placeableImprovements(w.grid, idx, engineer, p.tech());
    CHECK(contains(options, ImprovementType::Railway));         // replaces the road
    CHECK(aoc::sim::requestPlaceImprovement(w.gameState, w.grid, PlayerId{0}, at, ImprovementType::Railway) == ErrorCode::Ok);
    CHECK(w.grid.improvement(idx) == ImprovementType::Railway);
    CHECK(engineer.chargesRemaining() == 0);
    CHECK(aoc::sim::requestChop(w.gameState, w.grid, PlayerId{0}, at) == ErrorCode::InvalidArgument); // engineers do not chop
}
