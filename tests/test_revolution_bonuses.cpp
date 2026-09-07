/**
 * @file test_revolution_bonuses.cpp
 * @brief Every column of REVOLUTION_DEFS::bonuses reaches the game. Production
 *        and gold-per-citizen always did; science, trade capacity and pollution
 *        were computed by accessors nobody called, so reaching an industrial age
 *        paid only half of what its table row promised.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/Player.hpp"
#include "aoc/simulation/economy/IndustrialRevolution.hpp"

using aoc::sim::IndustrialRevolutionId;
using aoc::sim::PlayerIndustrialComponent;
using aoc::sim::REVOLUTION_DEFS;

namespace {

/// A component that has reached `id`.
PlayerIndustrialComponent at(IndustrialRevolutionId id) {
    PlayerIndustrialComponent ind{};
    ind.currentRevolution = id;
    return ind;
}

} // namespace

TEST_CASE("a civ that has reached no revolution gets a neutral multiplier") {
    const PlayerIndustrialComponent none = at(IndustrialRevolutionId::None);
    CHECK(none.cumulativeScienceMultiplier() == doctest::Approx(1.0f));
    CHECK(none.cumulativeTradeMultiplier() == doctest::Approx(1.0f));
    CHECK(none.cumulativeProductionMultiplier() == doctest::Approx(1.0f));
    CHECK(aoc::sim::revolutionPollutionMultiplier(none) == doctest::Approx(1.0f));
}

TEST_CASE("each further revolution compounds, never shrinks") {
    // The relationship, not the constants: a balance pass may retune the table,
    // but reaching a later age must never leave a civ worse off.
    float prevScience    = 1.0f;
    float prevTrade      = 1.0f;
    float prevProduction = 1.0f;

    for (uint8_t r = 1; r <= static_cast<uint8_t>(REVOLUTION_DEFS.size()); ++r) {
        const PlayerIndustrialComponent ind = at(static_cast<IndustrialRevolutionId>(r));
        CHECK(ind.cumulativeScienceMultiplier() >= prevScience);
        CHECK(ind.cumulativeTradeMultiplier() >= prevTrade);
        CHECK(ind.cumulativeProductionMultiplier() >= prevProduction);
        prevScience    = ind.cumulativeScienceMultiplier();
        prevTrade      = ind.cumulativeTradeMultiplier();
        prevProduction = ind.cumulativeProductionMultiplier();
    }
}

TEST_CASE("the science and trade columns are not all neutral") {
    // If every row were 1.0 the wiring would be untestable and the column
    // pointless. At least one revolution must actually pay.
    const PlayerIndustrialComponent last =
        at(static_cast<IndustrialRevolutionId>(REVOLUTION_DEFS.size()));
    CHECK(last.cumulativeScienceMultiplier() > 1.0f);
    CHECK(last.cumulativeTradeMultiplier() > 1.0f);
}

TEST_CASE("a later industrial age is dirtier than none at all") {
    // The column is documented as "how much pollution is generated".
    const PlayerIndustrialComponent none  = at(IndustrialRevolutionId::None);
    const PlayerIndustrialComponent first = at(IndustrialRevolutionId::First);
    CHECK(aoc::sim::revolutionPollutionMultiplier(first) >
          aoc::sim::revolutionPollutionMultiplier(none));
}

TEST_CASE("hasAutomation names the third revolution") {
    // The robot-slot site tests this accessor rather than re-checking the enum,
    // which is what previously left it looking dead.
    CHECK_FALSE(at(IndustrialRevolutionId::Second).hasAutomation());
    CHECK(at(IndustrialRevolutionId::Third).hasAutomation());
    CHECK(at(IndustrialRevolutionId::Fourth).hasAutomation());
}

TEST_CASE("every revolution row still sits at its own id") {
    for (std::size_t i = 0; i < REVOLUTION_DEFS.size(); ++i) {
        CHECK(static_cast<std::size_t>(REVOLUTION_DEFS[i].id) == i + 1);
    }
}
