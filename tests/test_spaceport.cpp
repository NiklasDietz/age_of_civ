/**
 * @file test_spaceport.cpp
 * @brief The Spaceport (46) is a real building: its row, its Surface Plate unlock, and
 *        the space race gating on it instead of a Campus. Until 2026-09-05 the
 *        Spaceport existed only in comments and any player with a Campus ran the
 *        space race.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/victory/SpaceRace.hpp"

using aoc::BuildingId;
using aoc::PlayerId;
using aoc::TechId;
using aoc::sim::BUILDING_DEFS;
using aoc::sim::buildingDef;
using aoc::sim::DistrictType;

namespace {

constexpr BuildingId SPACEPORT{46};
constexpr TechId SURFACE_PLATE{18}; // the first space project's own tech

/// Mark `tech` completed for `player`, growing the bitfields if the fixture left them empty.
void grantTech(aoc::game::Player& player, TechId tech) {
    aoc::sim::PlayerTechComponent& t = player.tech();
    if (t.completedTechs.size() <= tech.value) {
        t.completedTechs.resize(tech.value + 1, false);
    }
    if (t.knownTechs.size() <= tech.value) {
        t.knownTechs.resize(tech.value + 1, false);
    }
    t.completedTechs[tech.value] = true;
    t.knownTechs[tech.value]     = true;
}

} // namespace

TEST_CASE("the Spaceport has a row in the Industrial Zone and Surface Plate unlocks it") {
    CHECK(BUILDING_DEFS.size() >= 47);
    const aoc::sim::BuildingDef& def = buildingDef(SPACEPORT);
    CHECK(def.name == "Spaceport");
    CHECK(def.requiredDistrict == DistrictType::Industrial);
    CHECK(def.productionCost == 300);
    CHECK(def.scienceBonus == 2);
    CHECK_FALSE(def.hasResourceCost());

    bool unlocked = false;
    for (const aoc::sim::TechDef& tech : aoc::sim::allTechs()) {
        for (const BuildingId bid : tech.unlockedBuildings) {
            if (bid == SPACEPORT) {
                CHECK(tech.id == SURFACE_PLATE);
                unlocked = true;
            }
        }
    }
    CHECK(unlocked);
}

TEST_CASE("the space race needs a Spaceport, a Campus alone no longer counts") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha");
    aoc::game::Player& player = *w.gameState.player(PlayerId{0});
    aoc::game::City& city     = *player.cities()[0];
    grantTech(player, aoc::sim::SPACE_PROJECT_DEFS[0].requiredTech);
    REQUIRE(player.hasResearched(aoc::sim::SPACE_PROJECT_DEFS[0].requiredTech));

    // A Campus, the old stand-in gate: nothing happens.
    city.districts().districts.push_back({DistrictType::Campus, {6, 5}, {}});
    REQUIRE(city.hasDistrict(DistrictType::Campus));
    aoc::sim::processSpaceRace(w.gameState, w.grid);
    CHECK(player.spaceRace().progress[0] == doctest::Approx(0.0f));

    // A Spaceport in an Industrial Zone: the first project starts accumulating.
    city.districts().districts.push_back({DistrictType::Industrial, {4, 5}, {SPACEPORT}});
    REQUIRE(city.hasBuilding(SPACEPORT));
    aoc::sim::processSpaceRace(w.gameState, w.grid);
    CHECK(player.spaceRace().progress[0] > 0.0f);
    CHECK_FALSE(player.spaceRace().completed[0]);

    // The rival has neither and stays at zero.
    CHECK(w.gameState.player(PlayerId{1})->spaceRace().progress[0] == doctest::Approx(0.0f));
}
