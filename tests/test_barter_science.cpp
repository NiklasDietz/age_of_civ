/**
 * @file test_barter_science.cpp
 * @brief A Barter civ with no coins is not charged for science.
 *
 *        Its treasury was forced to zero every turn, so the science-funding
 *        step found nothing affordable and applied its 50% floor: every civ
 *        researched at half speed for as long as it stayed in Barter, which
 *        on the blessed seeds is over half of all player-turns.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/city/CityScience.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/resource/EconomySimulation.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/turn/TurnProcessor.hpp"

#include <utility>

using aoc::PlayerId;

namespace {

struct Progress {
    float barter  = 0.0f;
    float coinage = 0.0f;
    float science = 0.0f; ///< what one turn of the coinage twin should yield unpenalised
};

/// Two identical one-city civs; returns each one's research progress after a turn.
Progress researchAfterOneTurn(aoc::sim::MonetarySystemType system0,
                              aoc::sim::MonetarySystemType system1) {
    aoc::test::World w = aoc::test::makeWorld(2);
    // Populous enough that 20% of the science rounds to a real funding charge,
    // small enough that the first tech does not complete within the turn.
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha").setPopulation(4);
    aoc::test::addCityAt(w, PlayerId{1}, 15, 9, "Beta").setPopulation(4);
    aoc::game::Player& a = *w.gameState.player(PlayerId{0});
    aoc::game::Player& b = *w.gameState.player(PlayerId{1});
    a.monetary().system = system0;
    b.monetary().system = system1;
    a.setTreasury(0);
    b.setTreasury(0);
    // Nothing reaches either treasury during the turn, so the funding step
    // sees exactly the zero it was given.
    a.monetary().goldAllocation = 0.0f;
    b.monetary().goldAllocation = 0.0f;
    a.tech().currentResearch = aoc::TechId{0};
    b.tech().currentResearch = aoc::TechId{0};

    aoc::sim::EconomySimulation economy;
    aoc::sim::DiplomacyManager diplomacy;
    diplomacy.initialize(2);
    aoc::Random rng{11u};
    aoc::sim::TurnContext ctx;
    ctx.gameState   = &w.gameState;
    ctx.grid        = &w.grid;
    ctx.economy     = &economy;
    ctx.diplomacy   = &diplomacy;
    ctx.rng         = &rng;
    ctx.allPlayers  = {PlayerId{0}, PlayerId{1}};
    ctx.currentTurn = 1;
    const float science = aoc::sim::computePlayerScience(b, w.grid);
    aoc::sim::processTurn(ctx);
    return {a.tech().researchProgress, b.tech().researchProgress, science};
}

} // namespace

TEST_CASE("a Barter civ with no coins researches at full speed") {
    using aoc::sim::MonetarySystemType;
    const Progress p = researchAfterOneTurn(MonetarySystemType::Barter, MonetarySystemType::CommodityMoney);
    REQUIRE(p.science * 0.2f >= 1.0f); // the funding charge is real, or the control proves nothing
    REQUIRE(p.barter > 0.0f);
    // The coinage twin holds no money, so it still hits the 50% floor:
    // that is the positive control, and the gap that used to hit Barter too.
    CHECK(p.coinage == doctest::Approx(p.barter * 0.5f).epsilon(0.02));
}
