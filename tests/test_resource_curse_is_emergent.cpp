/**
 * @file test_resource_curse_is_emergent.cpp
 * @brief The resource curse is labour competition, not a coefficient.
 *
 *        ResourceCurse.cpp computed a manufacturing penalty from the ratio of
 *        raw to total output value and multiplied it into every processed
 *        recipe. That formula existed to stand in for a mechanism the sim had
 *        disabled by accident: a city's citizens were counted TWICE, once to
 *        work tiles and again to staff recipes, so a pop-10 city worked 10
 *        tiles AND got 5 recipe slots from the same 10 people. Putting
 *        everyone down the mine cost nothing in factory capacity.
 *
 *        workerSlots' own comment always described the intended mechanism:
 *        "resource-rich cities put workers on mines, leaving fewer for
 *        factories -- this naturally creates the resource curse". One labour
 *        pool makes that true, and the coefficient is gone.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/production/Automation.hpp"
#include "aoc/simulation/resource/EconomySimulation.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

using aoc::PlayerId;

namespace {

constexpr int32_t CITY_POP = 12;

/// A city of fixed population working `mines` resource tiles plus `plain`
/// ordinary ones. Returns how many processed goods it managed to make.
///
/// Raw inputs are stocked generously and identically in both cases, so the
/// only variable is how many of the city's citizens are standing on mines
/// rather than being available to staff a recipe. Without that, a city with no
/// mines has no ore either and both cases produce nothing -- which is how the
/// first version of this test managed to pass its own premise as 0 < 0.
int32_t processedOutputWith(int32_t mines, int32_t plain) {
    aoc::test::World w    = aoc::test::makeWorld(1);
    aoc::game::City& city = aoc::test::addCityAt(w, PlayerId{0}, 8, 8, "Works");
    city.setPopulation(CITY_POP);

    // Ring 1 and 2 around the centre: the first `mines` carry iron, the next
    // `plain` carry nothing. Same tile count either way, so the only variable
    // is what the citizens are standing on.
    std::vector<aoc::hex::AxialCoord> ring;
    aoc::hex::ring(city.location(), 1, std::back_inserter(ring));
    aoc::hex::ring(city.location(), 2, std::back_inserter(ring));
    REQUIRE(static_cast<int32_t>(ring.size()) >= mines + plain);

    for (int32_t i = 0; i < mines + plain; ++i) {
        const aoc::hex::AxialCoord tile = ring[static_cast<std::size_t>(i)];
        if (!w.grid.isValid(tile)) { continue; }
        if (i < mines) {
            w.grid.setResource(w.grid.toIndex(tile),
                               aoc::ResourceId{aoc::sim::goods::IRON_ORE});
        }
        city.workedTiles().push_back(tile);
    }

    // A Forge and a Workshop, so the tier-1 recipes have somewhere to run.
    // Without a required building nothing fires and both arms of the
    // comparison sit at zero.
    REQUIRE_FALSE(city.districts().districts.empty());
    city.districts().districts[0].buildings.push_back(aoc::BuildingId{0}); // Forge
    city.districts().districts[0].buildings.push_back(aoc::BuildingId{1}); // Workshop

    // Same raw inputs either way.
    for (uint16_t g = 0; g < aoc::sim::goodCount(); ++g) {
        const aoc::sim::GoodCategory cat = aoc::sim::goodDef(g).category;
        if (cat == aoc::sim::GoodCategory::RawStrategic ||
            cat == aoc::sim::GoodCategory::RawBonus) {
            city.stockpile().addGoods(g, 400);
        }
    }

    aoc::sim::EconomySimulation econ;
    econ.initialize();
    // Several turns: raw ore has to be mined before a recipe can consume it,
    // so a single turn would show nothing either way.
    for (int32_t turn = 0; turn < 12; ++turn) {
        w.gameState.setCurrentTurn(turn + 1);
        econ.executeTurn(w.gameState, w.grid);
    }

    int32_t processed = 0;
    for (uint16_t g = 0; g < aoc::sim::goodCount(); ++g) {
        const aoc::sim::GoodCategory cat = aoc::sim::goodDef(g).category;
        if (cat == aoc::sim::GoodCategory::Processed ||
            cat == aoc::sim::GoodCategory::Advanced) {
            processed += city.stockpile().getAmount(g);
        }
    }
    return processed;
}

} // namespace

TEST_CASE("a city that mans its mines has fewer hands for its factories") {
    // Same population, same number of worked tiles. The only difference is how
    // many of those tiles are extraction. This is the curse, and it is now the
    // mechanism rather than a multiplier applied after the fact.
    const int32_t industrial  = processedOutputWith(/*mines=*/0, /*plain=*/8);
    const int32_t extractive  = processedOutputWith(/*mines=*/8, /*plain=*/0);
    CHECK(extractive < industrial);
}

TEST_CASE("a city with every citizen down a mine has no industry at all") {
    // The limit case of one labour pool. Note the units: a recipe slot is two
    // citizens (totalWorkerCapacity is pop/2), so exhausting the pool takes a
    // miner per CITIZEN, not per slot -- charging a slot per miner would bill
    // each one twice, which an earlier version of this change did.
    const int32_t swamped = processedOutputWith(/*mines=*/CITY_POP, /*plain=*/0);
    CHECK(swamped == 0);

    // Half of them mining still leaves a working industry.
    const int32_t halfMining = processedOutputWith(/*mines=*/CITY_POP / 2, /*plain=*/0);
    CHECK(halfMining > 0);
}

TEST_CASE("robots buy back the capacity the mines cost") {
    // Automation is the escape valve, and it is the right one: a resource
    // economy mechanises to recover the industrial capacity its mines take.
    const int32_t withoutRobots = aoc::sim::totalWorkerCapacity(CITY_POP, 0);
    const int32_t withRobots    = aoc::sim::totalWorkerCapacity(CITY_POP, 4);
    CHECK(withRobots > withoutRobots);
    CHECK(withRobots - withoutRobots == 4);
}
