/**
 * @file test_goods_economy.cpp
 * @brief The marginal unit of a finished good is worth something.
 *
 *        Two halves of one problem. Consuming goods was pure destruction: the
 *        population drains removed them with no reward for meeting demand and
 *        no penalty for failing it -- one drain's own comment said no
 *        downstream effect read its result. And the rewards for merely HOLDING
 *        goods saturated almost at once: amenities capped at 4x base, and the
 *        goods tax counted four goods and capped at a flat 15 per city, so
 *        Software (base price 200) and Microchips (160) earned nothing at all.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/city/Happiness.hpp"
#include "aoc/simulation/economy/Maintenance.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

using aoc::PlayerId;
using aoc::sim::CONSUMER_SATISFACTION_AMENITIES;
using aoc::sim::GOODS_AMENITY_CAP_MULTIPLE;

namespace {

aoc::game::City& oneCity(aoc::test::World& w) {
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha");
    return *w.gameState.player(PlayerId{0})->cities()[0];
}

} // namespace

// ============================================================================
// Half one: consumption pays, and lacking hurts
// ============================================================================

TEST_CASE("a city that met its consumer demand is happier than one that did not") {
    const auto amenitiesAt = [](float satisfaction) {
        aoc::test::World w                    = aoc::test::makeWorld(1);
        aoc::game::City& city                 = oneCity(w);
        aoc::game::Player& player             = *w.gameState.player(PlayerId{0});
        // Enough base amenities that the starved twin stays above the zero
        // floor: since the luxury variety model, a city with no luxuries
        // carries a shortfall penalty and the slider is worth 2, not 5.
        player.monetary().luxuryAllocation    = 1.0f;
        city.happiness().consumerSatisfaction = satisfaction;
        aoc::sim::computeCityHappiness(player);
        return city.happiness().amenities;
    };

    const float met     = amenitiesAt(1.0f);
    const float partial = amenitiesAt(0.5f);
    const float starved = amenitiesAt(0.0f);

    CHECK(met > partial);
    CHECK(partial > starved);
    // Half the swing up, half down: meeting demand is a reward, failing it a
    // penalty, rather than merely the absence of a reward.
    CHECK(met - starved == doctest::Approx(CONSUMER_SATISFACTION_AMENITIES));
}

TEST_CASE("full satisfaction is worth a reward, not just neutrality") {
    aoc::test::World w        = aoc::test::makeWorld(1);
    aoc::game::City& city     = oneCity(w);
    aoc::game::Player& player = *w.gameState.player(PlayerId{0});

    city.happiness().consumerSatisfaction = 0.5f;
    aoc::sim::computeCityHappiness(player);
    const float neutral = city.happiness().amenities;

    city.happiness().consumerSatisfaction = 1.0f;
    aoc::sim::computeCityHappiness(player);
    CHECK(city.happiness().amenities > neutral);
}

// ============================================================================
// Half two: the holding rewards no longer saturate immediately
// ============================================================================

TEST_CASE("a well-stocked city is distinguishable from a barely-stocked one") {
    // The cap was 4x base, which sixteen units reached; everything past that
    // was worth nothing.
    CHECK(GOODS_AMENITY_CAP_MULTIPLE > 4.0f);

    const auto amenitiesWith = [](int32_t units) {
        aoc::test::World w                    = aoc::test::makeWorld(1);
        aoc::game::City& city                 = oneCity(w);
        aoc::game::Player& player             = *w.gameState.player(PlayerId{0});
        city.happiness().consumerSatisfaction = 1.0f;
        city.stockpile().addGoods(aoc::sim::goods::CLOTHING, units);
        aoc::sim::computeCityHappiness(player);
        return city.happiness().amenities;
    };

    // Twenty units used to be worth exactly the same as sixteen.
    CHECK(amenitiesWith(25) > amenitiesWith(16));
    // But it is still bounded: happiness must not become a warehousing game.
    CHECK(amenitiesWith(100000) == doctest::Approx(amenitiesWith(10000)));
}

TEST_CASE("the goods tax counts the goods worth the most") {
    // Only ELECTRONICS among the high-value finished goods was counted, so the
    // most valuable things a civ can make contributed nothing to its economy.
    aoc::test::World w    = aoc::test::makeWorld(1);
    aoc::game::City& city = oneCity(w);
    city.setPopulation(10);

    const aoc::CurrencyAmount bare = aoc::sim::cityGoodsTax(city);

    city.stockpile().addGoods(aoc::sim::goods::SOFTWARE, 1);
    const aoc::CurrencyAmount withSoftware = aoc::sim::cityGoodsTax(city);
    CHECK(withSoftware > bare);

    city.stockpile().addGoods(aoc::sim::goods::MICROCHIPS, 1);
    CHECK(aoc::sim::cityGoodsTax(city) > withSoftware);
}

TEST_CASE("a valuable good is taxed more heavily than a cheap one") {
    const auto taxWith = [](uint16_t good) {
        aoc::test::World w    = aoc::test::makeWorld(1);
        aoc::game::City& city = oneCity(w);
        city.setPopulation(20); // a cap high enough not to mask the difference
        city.stockpile().addGoods(good, 4);
        return aoc::sim::cityGoodsTax(city);
    };
    CHECK(taxWith(aoc::sim::goods::SOFTWARE) > taxWith(aoc::sim::goods::CONSUMER_GOODS));
}

TEST_CASE("the tax ceiling scales with the city, and still binds") {
    // A flat 15 for every city meant a modest stockpile reached the ceiling and
    // every further unit was worth nothing.
    CHECK(aoc::sim::goodsTaxCap(20) > aoc::sim::goodsTaxCap(2));

    aoc::test::World w    = aoc::test::makeWorld(1);
    aoc::game::City& city = oneCity(w);
    city.setPopulation(5);
    city.stockpile().addGoods(aoc::sim::goods::SOFTWARE, 100000);
    // Absurd wealth is still bounded by what the city can absorb.
    CHECK(aoc::sim::cityGoodsTax(city) == aoc::sim::goodsTaxCap(5));
}

TEST_CASE("an empty city is taxed nothing") {
    aoc::test::World w    = aoc::test::makeWorld(1);
    aoc::game::City& city = oneCity(w);
    CHECK(aoc::sim::cityGoodsTax(city) == 0);
}

TEST_CASE("no opinion on demand the civ cannot yet supply") {
    // Consumer goods are manufactured, so no city holds any before a Workshop
    // exists. Scoring that as unmet demand penalised every city from turn one,
    // and since happiness feeds loyalty at double weight the map collapsed into
    // free cities -- three of four civs eliminated by turn 211 on seed 42. A
    // city cannot be blamed for lacking what nobody can produce yet.
    //
    // The neutral point of the amenity term is 0.5: neither reward nor penalty.
    aoc::test::World w        = aoc::test::makeWorld(1);
    aoc::game::City& city     = oneCity(w);
    aoc::game::Player& player = *w.gameState.player(PlayerId{0});
    REQUIRE_FALSE(city.hasBuilding(aoc::BuildingId{1})); // no Workshop

    city.happiness().consumerSatisfaction = 0.5f;
    aoc::sim::computeCityHappiness(player);
    const float neutralAmenities = city.happiness().amenities;

    // A starved city, once supply IS possible, sits below that neutral point.
    city.happiness().consumerSatisfaction = 0.0f;
    aoc::sim::computeCityHappiness(player);
    CHECK(city.happiness().amenities < neutralAmenities);
}
