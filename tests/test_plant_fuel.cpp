/**
 * @file test_plant_fuel.cpp
 * @brief Power plants burn their listed fuel exactly once a turn. Until
 *        2026-09-07 they were charged twice -- once by
 *        `EconomySimulation::consumeBuildingFuel` via `BuildingDef.ongoingFuel`
 *        and again by `computeCityPower` via `PowerPlantDef.fuelPerTurn` -- so
 *        a plant needed double its listed fuel to produce any power at all, and
 *        the Coal Plant needed triple because the two tables disagreed on the
 *        amount. The grid is the single owner now; these tests pin that.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/production/PowerGrid.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

using aoc::BuildingId;
using aoc::PlayerId;

namespace {

constexpr BuildingId COAL_PLANT{26};
constexpr BuildingId NUCLEAR_PLANT{29};

/// The plant row in POWER_PLANT_DEFS for `bid`.
const aoc::sim::PowerPlantDef& plantFor(BuildingId bid) {
    for (const aoc::sim::PowerPlantDef& def : aoc::sim::POWER_PLANT_DEFS) {
        if (def.buildingId == bid) {
            return def;
        }
    }
    FAIL("no power plant row for that building");
    return aoc::sim::POWER_PLANT_DEFS[0];
}

/// Give `city` a plant and `fuel` units of the good it burns.
aoc::game::City& cityWithPlant(aoc::test::World& w, BuildingId bid, int32_t fuel) {
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha");
    aoc::game::City& city = *w.gameState.player(PlayerId{0})->cities()[0];
    city.districts().districts[0].buildings.push_back(bid);
    const aoc::sim::PowerPlantDef& def = plantFor(bid);
    city.stockpile().addGoods(def.fuelGoodId, fuel);
    return city;
}

} // namespace

TEST_CASE("a fuelled Nuclear Plant burns its uranium exactly once per turn") {
    const aoc::sim::PowerPlantDef& def = plantFor(NUCLEAR_PLANT);
    REQUIRE(def.fuelGoodId != 0xFFFF);

    aoc::test::World w    = aoc::test::makeWorld(1);
    aoc::game::City& city = cityWithPlant(w, NUCLEAR_PLANT, def.fuelPerTurn);

    const int32_t before = city.stockpile().getAmount(def.fuelGoodId);
    REQUIRE(before == def.fuelPerTurn);

    aoc::sim::CityPowerComponent power = aoc::sim::computeCityPower(w.gameState, w.grid, city);

    // Exactly one charge: the stockpile is drained by fuelPerTurn, not 2x.
    CHECK(city.stockpile().getAmount(def.fuelGoodId) == before - def.fuelPerTurn);
    // And with exactly its listed fuel the plant runs -- it used to need double.
    CHECK(power.energySupply >= def.energyOutput);
    CHECK(power.hasNuclear);
}

TEST_CASE("a Coal Plant burns its listed fuel once, not the sum of two tables") {
    // The Coal Plant was the worst case: PowerPlantDef says 2 coal, the
    // building row said 1 more, so it drained 3 and needed 3 to run.
    const aoc::sim::PowerPlantDef& def = plantFor(COAL_PLANT);
    REQUIRE(def.fuelGoodId == aoc::sim::goods::COAL);

    aoc::test::World w    = aoc::test::makeWorld(1);
    aoc::game::City& city = cityWithPlant(w, COAL_PLANT, def.fuelPerTurn);

    aoc::sim::CityPowerComponent power = aoc::sim::computeCityPower(w.gameState, w.grid, city);

    CHECK(city.stockpile().getAmount(aoc::sim::goods::COAL) == 0);
    CHECK(power.energySupply >= def.energyOutput);
}

TEST_CASE("an unfuelled plant burns nothing and produces nothing") {
    const aoc::sim::PowerPlantDef& def = plantFor(NUCLEAR_PLANT);

    aoc::test::World w = aoc::test::makeWorld(1);
    // One short of what the plant needs.
    aoc::game::City& city = cityWithPlant(w, NUCLEAR_PLANT, def.fuelPerTurn - 1);

    const int32_t before               = city.stockpile().getAmount(def.fuelGoodId);
    aoc::sim::CityPowerComponent power = aoc::sim::computeCityPower(w.gameState, w.grid, city);

    // All-or-nothing: a plant that cannot run burns no fuel at all.
    CHECK(city.stockpile().getAmount(def.fuelGoodId) == before);
    CHECK(power.energySupply == 0);
    CHECK_FALSE(power.hasNuclear);
}

TEST_CASE("a plant draws on a sibling city when its own stockpile is short") {
    // Sibling pooling moved from consumeBuildingFuel into the grid with the
    // charge; without it a city with an empty stockpile would go dark beside a
    // neighbour sitting on surplus.
    const aoc::sim::PowerPlantDef& def = plantFor(NUCLEAR_PLANT);

    aoc::test::World w    = aoc::test::makeWorld(1);
    aoc::game::City& city = cityWithPlant(w, NUCLEAR_PLANT, 0); // no local fuel
    aoc::test::addCityAt(w, PlayerId{0}, 9, 9, "Beta");
    aoc::game::City& sibling = *w.gameState.player(PlayerId{0})->cities()[1];
    sibling.stockpile().addGoods(def.fuelGoodId, def.fuelPerTurn);

    aoc::sim::CityPowerComponent power = aoc::sim::computeCityPower(w.gameState, w.grid, city);

    CHECK(power.energySupply >= def.energyOutput);
    CHECK(sibling.stockpile().getAmount(def.fuelGoodId) == 0); // drawn from the sibling
}

TEST_CASE("every power plant's two fuel tables now name the same good") {
    // The double charge was only invisible because both tables agreed on the
    // good. If a future row disagrees, that is a content bug worth catching.
    for (const aoc::sim::PowerPlantDef& def : aoc::sim::POWER_PLANT_DEFS) {
        const aoc::sim::BuildingDef& bdef = aoc::sim::buildingDef(def.buildingId);
        if (!bdef.needsFuel()) {
            continue;
        }
        CHECK(bdef.ongoingFuelGoodId == def.fuelGoodId);
    }
}
