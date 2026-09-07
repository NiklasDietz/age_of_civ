/**
 * @file test_wonder_gates.cpp
 * @brief The three things the wonder system described but did not do: a
 *        national-versus-global distinction, strategic resource prerequisites,
 *        and Great Work slots on wonders.
 *
 *        GlobalWonderTracker applied one-per-game to all 24 because WonderDef
 *        had no flag to tell the two kinds apart. WonderLockReason::NoResource
 *        existed as a UI string no code produced. greatWorkCapacity summed
 *        district buildings only and never consulted city.wonders().
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/culture/GreatWorks.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/tech/TechGating.hpp"
#include "aoc/simulation/wonder/Wonder.hpp"

using aoc::PlayerId;
using aoc::sim::WONDER_COUNT;
using aoc::sim::wonderDef;
using aoc::sim::WonderLockReason;

namespace {

/// The first wonder marked national, and the first marked global.
[[nodiscard]] int32_t firstNational() {
    for (int32_t i = 0; i < WONDER_COUNT; ++i) {
        if (wonderDef(static_cast<aoc::sim::WonderId>(i)).national) {
            return i;
        }
    }
    return -1;
}
[[nodiscard]] int32_t firstGlobal() {
    for (int32_t i = 0; i < WONDER_COUNT; ++i) {
        if (!wonderDef(static_cast<aoc::sim::WonderId>(i)).national) {
            return i;
        }
    }
    return -1;
}

/// The named player's first city.
[[nodiscard]] const aoc::game::City& cityOf(const aoc::test::World& w, PlayerId p) {
    return *w.gameState.player(p)->cities()[0];
}

} // namespace

TEST_CASE("the table has both national and global wonders") {
    // If every wonder were one kind the distinction would be untestable, and
    // the flag would be as inert as its absence was.
    CHECK(firstNational() >= 0);
    CHECK(firstGlobal() >= 0);
}

TEST_CASE("a global wonder built by one civ locks out the others") {
    const int32_t id = firstGlobal();
    REQUIRE(id >= 0);
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha");
    aoc::test::addCityAt(w, PlayerId{1}, 15, 9, "Beta");

    w.gameState.wonderTracker().markBuilt(static_cast<aoc::sim::WonderId>(id), PlayerId{0});

    CHECK(aoc::sim::wonderLockReason(w.gameState, PlayerId{1}, cityOf(w, PlayerId{1}),
                                     static_cast<uint8_t>(id)) ==
          static_cast<uint8_t>(WonderLockReason::AlreadyBuilt));
}

TEST_CASE("a national wonder built by one civ leaves the others free") {
    const int32_t id = firstNational();
    REQUIRE(id >= 0);
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha");
    aoc::test::addCityAt(w, PlayerId{1}, 15, 9, "Beta");

    w.gameState.wonderTracker().markBuilt(static_cast<aoc::sim::WonderId>(id), PlayerId{0});

    // Whatever else may gate it, it is not "someone else already built it".
    CHECK(aoc::sim::wonderLockReason(w.gameState, PlayerId{1}, cityOf(w, PlayerId{1}),
                                     static_cast<uint8_t>(id)) !=
          static_cast<uint8_t>(WonderLockReason::AlreadyBuilt));
}

TEST_CASE("a civ still cannot build the same national wonder twice") {
    const int32_t id = firstNational();
    REQUIRE(id >= 0);
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha");
    aoc::game::City& city = *w.gameState.player(PlayerId{0})->cities()[0];
    city.wonders().wonders.push_back(static_cast<aoc::sim::WonderId>(id));

    CHECK(aoc::sim::wonderLockReason(w.gameState, PlayerId{0}, cityOf(w, PlayerId{0}),
                                     static_cast<uint8_t>(id)) ==
          static_cast<uint8_t>(WonderLockReason::AlreadyOwned));
}

TEST_CASE("a resource-gated wonder reports NoResource until the resource is held") {
    int32_t gated = -1;
    for (int32_t i = 0; i < WONDER_COUNT; ++i) {
        if (wonderDef(static_cast<aoc::sim::WonderId>(i)).needsResource()) {
            gated = i;
            break;
        }
    }
    REQUIRE(gated >= 0); // no wonder names a resource: the column would be inert

    const aoc::sim::WonderDef& def = wonderDef(static_cast<aoc::sim::WonderId>(gated));
    aoc::test::World w             = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha");
    aoc::game::City& city = *w.gameState.player(PlayerId{0})->cities()[0];

    CHECK(aoc::sim::wonderLockReason(w.gameState, PlayerId{0}, cityOf(w, PlayerId{0}),
                                     static_cast<uint8_t>(gated)) ==
          static_cast<uint8_t>(WonderLockReason::NoResource));

    // Stock exactly what it asks for and the reason goes away.
    city.stockpile().addGoods(def.requiredResource.value, def.requiredResourceAmount);
    CHECK(aoc::sim::wonderLockReason(w.gameState, PlayerId{0}, cityOf(w, PlayerId{0}),
                                     static_cast<uint8_t>(gated)) !=
          static_cast<uint8_t>(WonderLockReason::NoResource));
}

TEST_CASE("a wonder with Great Work slots raises its city's capacity") {
    int32_t housing = -1;
    for (int32_t i = 0; i < WONDER_COUNT; ++i) {
        if (wonderDef(static_cast<aoc::sim::WonderId>(i)).greatWorksSlots > 0) {
            housing = i;
            break;
        }
    }
    REQUIRE(housing >= 0); // no wonder houses works: the column would be inert

    aoc::test::World w = aoc::test::makeWorld(1);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha");
    aoc::game::City& city = *w.gameState.player(PlayerId{0})->cities()[0];

    const int32_t before = aoc::sim::greatWorkCapacity(city);
    city.wonders().wonders.push_back(static_cast<aoc::sim::WonderId>(housing));
    const int32_t after = aoc::sim::greatWorkCapacity(city);

    CHECK(after == before + wonderDef(static_cast<aoc::sim::WonderId>(housing)).greatWorksSlots);
    CHECK(aoc::sim::freeGreatWorkSlots(city) > 0);
}
