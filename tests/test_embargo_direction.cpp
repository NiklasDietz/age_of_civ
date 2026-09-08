/**
 * @file test_embargo_direction.cpp
 * @brief Embargoes have a direction. setEmbargo used to write both halves of
 *        the pair, and there was no way to express a one-sided refusal, so a
 *        World Congress sanction against one civ fabricated N reciprocal
 *        embargoes: the sanctioned civ automatically embargoed everyone who had
 *        voted against it. FIXLIST H1.8 asked for the split.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/Player.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/diplomacy/WorldCongress.hpp"

using aoc::PlayerId;

namespace {

constexpr uint8_t SEATS = 4;

} // namespace

TEST_CASE("an embargo binds only the civ that declared it") {
    aoc::sim::DiplomacyManager dip;
    dip.initialize(SEATS);

    dip.setEmbargo(PlayerId{0}, PlayerId{1}, true);

    CHECK(dip.hasEmbargo(PlayerId{0}, PlayerId{1}));       // 0 refuses to trade with 1
    CHECK_FALSE(dip.hasEmbargo(PlayerId{1}, PlayerId{0})); // 1 has decided nothing
}

TEST_CASE("either side's embargo blocks trade between the two") {
    // A route needs both ends willing, so trade code asks hasAnyEmbargo.
    aoc::sim::DiplomacyManager dip;
    dip.initialize(SEATS);
    CHECK_FALSE(dip.hasAnyEmbargo(PlayerId{0}, PlayerId{1}));

    dip.setEmbargo(PlayerId{1}, PlayerId{0}, true); // only the second civ refuses
    CHECK(dip.hasAnyEmbargo(PlayerId{0}, PlayerId{1}));
    CHECK(dip.hasAnyEmbargo(PlayerId{1}, PlayerId{0})); // symmetric question
}

TEST_CASE("lifting one direction leaves the other standing") {
    aoc::sim::DiplomacyManager dip;
    dip.initialize(SEATS);

    dip.setEmbargo(PlayerId{0}, PlayerId{1}, true);
    dip.setEmbargo(PlayerId{1}, PlayerId{0}, true);
    REQUIRE(dip.hasEmbargo(PlayerId{0}, PlayerId{1}));
    REQUIRE(dip.hasEmbargo(PlayerId{1}, PlayerId{0}));

    dip.setEmbargo(PlayerId{0}, PlayerId{1}, false);
    CHECK_FALSE(dip.hasEmbargo(PlayerId{0}, PlayerId{1}));
    CHECK(dip.hasEmbargo(PlayerId{1}, PlayerId{0})); // still refusing
    CHECK(dip.hasAnyEmbargo(PlayerId{0}, PlayerId{1}));
}

TEST_CASE("a mutual embargo is available when both sides really do refuse") {
    aoc::sim::DiplomacyManager dip;
    dip.initialize(SEATS);

    dip.setMutualEmbargo(PlayerId{0}, PlayerId{1}, true);
    CHECK(dip.hasEmbargo(PlayerId{0}, PlayerId{1}));
    CHECK(dip.hasEmbargo(PlayerId{1}, PlayerId{0}));

    dip.setMutualEmbargo(PlayerId{0}, PlayerId{1}, false);
    CHECK_FALSE(dip.hasAnyEmbargo(PlayerId{0}, PlayerId{1}));
}

TEST_CASE("a Congress sanction does not make the target embargo the world back") {
    // The fabrication this split exists to stop: sanctioning one civ used to
    // create an embargo in BOTH directions for every other seat.
    aoc::test::World w = aoc::test::makeWorld(SEATS);
    aoc::sim::DiplomacyManager dip;
    dip.initialize(SEATS);

    constexpr PlayerId TARGET{2};
    aoc::sim::applySanctionsBegin(&dip, w.gameState, TARGET);

    for (uint8_t p = 0; p < SEATS; ++p) {
        const PlayerId id{p};
        if (id == TARGET) {
            continue;
        }
        // Everyone else refuses to trade with the sanctioned civ...
        CHECK(dip.hasEmbargo(id, TARGET));
        // ...and the sanctioned civ has not thereby refused them.
        CHECK_FALSE(dip.hasEmbargo(TARGET, id));
    }
}

TEST_CASE("lifting a sanction clears what it set") {
    aoc::test::World w = aoc::test::makeWorld(SEATS);
    aoc::sim::DiplomacyManager dip;
    dip.initialize(SEATS);

    constexpr PlayerId TARGET{2};
    aoc::sim::applySanctionsBegin(&dip, w.gameState, TARGET);
    aoc::sim::applySanctionsEnd(&dip, w.gameState, TARGET);

    for (uint8_t p = 0; p < SEATS; ++p) {
        const PlayerId id{p};
        if (id == TARGET) {
            continue;
        }
        CHECK_FALSE(dip.hasEmbargo(id, TARGET));
    }
}
