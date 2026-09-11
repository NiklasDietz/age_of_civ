/**
 * @file test_luxury_demand.cpp
 * @brief The luxury demand model of the money and trade programme (plan B3):
 *        one luxury list from the goods table, amenities from variety alone,
 *        an era-scaled variety target with a capped shortfall penalty, one
 *        unit of upkeep per held type per turn, needs only for luxuries that
 *        exist somewhere on the map, and a slider that cannot replace trade.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/simulation/city/Happiness.hpp"
#include "aoc/simulation/resource/EconomySimulation.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

#include <algorithm>

using aoc::PlayerId;
using aoc::sim::goods::MARBLE;
using aoc::sim::goods::SALT;
using aoc::sim::goods::SILK;
using aoc::sim::goods::WHEAT;
using aoc::sim::goods::WINE;

namespace {

constexpr PlayerId P0{0};
constexpr PlayerId P1{1};

[[nodiscard]] float amenitiesOf(aoc::test::World& w, PlayerId owner) {
    aoc::game::Player& p = *w.gameState.player(owner);
    aoc::sim::computeCityHappiness(p, nullptr);
    return p.cities().front()->happiness().amenities;
}

} // namespace

TEST_CASE("the goods table is the one luxury list, and salt and marble are on it") {
    const std::vector<uint16_t>& ids = aoc::sim::luxuryGoodIds();
    REQUIRE_FALSE(ids.empty());
    CHECK(std::is_sorted(ids.begin(), ids.end()));
    int32_t inTable = 0;
    for (uint16_t id = 0; id < aoc::sim::goods::GOOD_COUNT; ++id) {
        if (aoc::sim::goodDef(id).category == aoc::sim::GoodCategory::RawLuxury) {
            ++inTable;
        }
    }
    CHECK(static_cast<int32_t>(ids.size()) == inTable);
    CHECK(aoc::sim::isLuxuryGood(SALT));
    CHECK(aoc::sim::isLuxuryGood(MARBLE));
    CHECK(aoc::sim::isLuxuryGood(SILK));
    CHECK_FALSE(aoc::sim::isLuxuryGood(WHEAT));
    CHECK_FALSE(aoc::sim::isLuxuryGood(aoc::sim::goods::GOOD_COUNT));
}

TEST_CASE("variety, not quantity, is what luxuries give") {
    aoc::test::World w    = aoc::test::makeWorld(1);
    aoc::game::City& city = aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    aoc::game::Player& p  = *w.gameState.player(P0);
    const int32_t target  = aoc::sim::luxuryVarietyTarget(p);

    const float none = amenitiesOf(w, P0);
    city.stockpile().addGoods(SILK, 1);
    const float oneSilk = amenitiesOf(w, P0);
    city.stockpile().addGoods(SILK, 29);
    const float muchSilk = amenitiesOf(w, P0);
    city.stockpile().addGoods(WINE, 1);
    const float silkAndWine = amenitiesOf(w, P0);

    CHECK(muchSilk == doctest::Approx(oneSilk)); // thirty units buy no more than one
    CHECK(oneSilk - none == doctest::Approx(1.0f + aoc::sim::luxuryShortfallPenalty(0, target) -
                                            aoc::sim::luxuryShortfallPenalty(1, target)));
    CHECK(silkAndWine - oneSilk ==
          doctest::Approx(1.0f + aoc::sim::luxuryShortfallPenalty(1, target) -
                          aoc::sim::luxuryShortfallPenalty(2, target)));
}

TEST_CASE("the variety target grows with the era and the empire") {
    aoc::test::World w = aoc::test::makeWorld(1);
    aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    aoc::game::Player& p = *w.gameState.player(P0);
    CHECK(aoc::sim::luxuryVarietyTarget(p) == 2);
    p.era().currentEra = aoc::EraId{3};
    aoc::test::addCityAt(w, P0, 9, 5, "Beta");
    aoc::test::addCityAt(w, P0, 13, 5, "Gamma");
    aoc::test::addCityAt(w, P0, 17, 5, "Delta");
    CHECK(aoc::sim::luxuryVarietyTarget(p) == 2 + 3 + 2);
}

TEST_CASE("a shortfall costs a quarter amenity per missing type, capped at two") {
    CHECK(aoc::sim::luxuryShortfallPenalty(0, 2) == doctest::Approx(0.5f));
    CHECK(aoc::sim::luxuryShortfallPenalty(1, 2) == doctest::Approx(0.25f));
    CHECK(aoc::sim::luxuryShortfallPenalty(2, 2) == doctest::Approx(0.0f));
    CHECK(aoc::sim::luxuryShortfallPenalty(5, 2) == doctest::Approx(0.0f));
    CHECK(aoc::sim::luxuryShortfallPenalty(0, 40) == doctest::Approx(2.0f));
}

TEST_CASE("each held luxury type costs one unit a turn, from the city holding the most") {
    aoc::test::World w     = aoc::test::makeWorld(1);
    aoc::game::City& alpha = aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    aoc::game::City& beta  = aoc::test::addCityAt(w, P0, 9, 5, "Beta");
    alpha.stockpile().addGoods(SILK, 3);
    beta.stockpile().addGoods(SILK, 1);
    beta.stockpile().addGoods(WINE, 1);

    aoc::sim::EconomySimulation economy;
    economy.executeTurn(w.gameState, w.grid);

    CHECK(alpha.stockpile().getAmount(SILK) == 2); // the richer city paid
    CHECK(beta.stockpile().getAmount(SILK) == 1);
    CHECK(beta.stockpile().getAmount(WINE) == 0); // a single unit lasts one turn
    CHECK(w.gameState.player(P0)->economy().uniqueLuxuryCount == 2);
}

TEST_CASE("a luxury nobody holds is not a need; one a rival holds is") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    aoc::game::City& beta = aoc::test::addCityAt(w, P1, 14, 8, "Beta");
    beta.stockpile().addGoods(WINE, 5);

    aoc::sim::EconomySimulation economy;
    economy.executeTurn(w.gameState, w.grid);

    const std::unordered_map<uint16_t, int32_t>& needs =
        w.gameState.player(P0)->economy().totalNeeds;
    CHECK(needs.count(WINE) == 1);
    CHECK(needs.count(SILK) == 0);
    CHECK(w.gameState.player(P1)->economy().totalNeeds.count(WINE) == 0); // they have it
}

TEST_CASE("the luxury slider is worth two amenities at full, not five") {
    aoc::test::World w = aoc::test::makeWorld(1);
    aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    aoc::game::Player& p          = *w.gameState.player(P0);
    p.monetary().luxuryAllocation = 0.0f;
    const float off               = amenitiesOf(w, P0);
    p.monetary().luxuryAllocation = 1.0f;
    const float full              = amenitiesOf(w, P0);
    CHECK(full - off == doctest::Approx(aoc::sim::LUXURY_SLIDER_AMENITIES));
    CHECK(aoc::sim::LUXURY_SLIDER_AMENITIES == doctest::Approx(2.0f));
}
