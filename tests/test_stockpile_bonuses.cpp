/**
 * @file test_stockpile_bonuses.cpp
 * @brief Goods in a city stockpile raise amenities (Electronics) and
 *        accelerate research (Computers, Glass). Until 2026-09-07 both reads
 *        used the wrong good id: id 75 (Semiconductors) instead of 101
 *        (Electronics), and id 77 (Rubber Goods) instead of 107 (Computers).
 *        The tests here pin the correct ids so the same mistake cannot recur.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/city/Happiness.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/resource/EconomySimulation.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/turn/TurnProcessor.hpp"
#include "aoc/core/Random.hpp"

using aoc::PlayerId;
using aoc::TechId;

namespace {

/// A minimal turn context wired for one player.
aoc::sim::TurnContext makeCtx(aoc::test::World& w, aoc::sim::EconomySimulation& eco,
                              aoc::sim::DiplomacyManager& dip, aoc::Random& rng) {
    aoc::sim::TurnContext ctx;
    ctx.gameState   = &w.gameState;
    ctx.grid        = &w.grid;
    ctx.economy     = &eco;
    ctx.diplomacy   = &dip;
    ctx.rng         = &rng;
    ctx.allPlayers  = {PlayerId{0}};
    ctx.currentTurn = 1;
    return ctx;
}

/// One turn for player 0, returns the research progress gained.
float progressGained(aoc::test::World& w, aoc::sim::EconomySimulation& eco,
                     aoc::sim::DiplomacyManager& dip, aoc::Random& rng) {
    const float before        = w.gameState.player(PlayerId{0})->tech().researchProgress;
    aoc::sim::TurnContext ctx = makeCtx(w, eco, dip, rng);
    aoc::sim::processPlayerTurn(ctx, PlayerId{0});
    return w.gameState.player(PlayerId{0})->tech().researchProgress - before;
}

/// Build a world with one player, one city, enough gold to fund research, and
/// a very expensive tech queued so one turn never completes it.
aoc::test::World baseWorld() {
    aoc::test::World w = aoc::test::makeWorld(1);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha");
    // Nuclear Fission (TechId{17}) costs 1100 science: unreachable in one turn.
    w.gameState.player(PlayerId{0})->tech().currentResearch = TechId{17};
    // Enough gold to pay the 0.2 gold/science funding cost without cap.
    w.gameState.player(PlayerId{0})->setTreasury(100000);
    return w;
}

} // namespace

// ============================================================================
// Electronics amenity bonus
// ============================================================================

TEST_CASE("Electronics (101) raises amenities; Semiconductors (75) does not") {
    // The bug: Happiness.cpp used id 75 (Semiconductors) instead of 101 (Electronics).
    REQUIRE(aoc::sim::goods::ELECTRONICS == 101);
    REQUIRE(aoc::sim::goods::SEMICONDUCTORS == 75);

    aoc::test::World w = aoc::test::makeWorld(1);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha");
    aoc::game::Player& player = *w.gameState.player(PlayerId{0});
    aoc::game::City& city     = *player.cities()[0];

    aoc::sim::computeCityHappiness(player);
    const float base = city.happiness().amenities;

    // One unit of Electronics raises amenities (sqrt(1) * 0.8 = 0.8).
    city.stockpile().addGoods(aoc::sim::goods::ELECTRONICS, 1);
    aoc::sim::computeCityHappiness(player);
    const float withElectronics = city.happiness().amenities;
    CHECK(withElectronics > base);

    // Clear stockpile. One unit of Semiconductors must not raise amenities.
    city.stockpile().goods.clear();
    city.stockpile().addGoods(aoc::sim::goods::SEMICONDUCTORS, 1);
    aoc::sim::computeCityHappiness(player);
    const float withSemiconductors = city.happiness().amenities;
    CHECK(withSemiconductors == doctest::Approx(base));

    // The neighbour ids (74, 76) are also not Electronics.
    city.stockpile().goods.clear();
    city.stockpile().addGoods(static_cast<uint16_t>(74), 1);
    aoc::sim::computeCityHappiness(player);
    CHECK(city.happiness().amenities == doctest::Approx(base));

    city.stockpile().goods.clear();
    city.stockpile().addGoods(static_cast<uint16_t>(76), 1); // Glass (correct id, not Electronics)
    aoc::sim::computeCityHappiness(player);
    CHECK(city.happiness().amenities == doctest::Approx(base));
}

// ============================================================================
// Computers science bonus
// ============================================================================

TEST_CASE("Computers good (107) raises research speed; Rubber Goods (77) does not") {
    // The bug: TurnProcessor.cpp used id 77 (Rubber Goods) instead of 107 (Computers).
    REQUIRE(aoc::sim::goods::COMPUTERS_GOOD == 107);
    REQUIRE(aoc::sim::goods::RUBBER_GOODS == 77);

    aoc::sim::EconomySimulation eco;
    aoc::sim::DiplomacyManager dip;
    dip.initialize(1);
    aoc::Random rng{42u};

    // Baseline: no special goods.
    aoc::test::World wb = baseWorld();
    const float base    = progressGained(wb, eco, dip, rng);
    REQUIRE(base > 0.0f); // sanity: research did advance

    // Computers (107): should give +15 % science.
    {
        aoc::test::World wc = baseWorld();
        wc.gameState.player(PlayerId{0})
            ->cities()[0]
            ->stockpile()
            .addGoods(aoc::sim::goods::COMPUTERS_GOOD, 5);
        const float withComputers = progressGained(wc, eco, dip, rng);
        CHECK(withComputers > base);
        CHECK(withComputers / base == doctest::Approx(1.15f).epsilon(0.02f));
    }

    // Rubber Goods (77): must not trigger the computers bonus.
    {
        aoc::test::World wr = baseWorld();
        wr.gameState.player(PlayerId{0})
            ->cities()[0]
            ->stockpile()
            .addGoods(aoc::sim::goods::RUBBER_GOODS, 5);
        const float withRubber = progressGained(wr, eco, dip, rng);
        CHECK(withRubber == doctest::Approx(base).epsilon(0.01f));
    }

    // Neighbour id 108 is also not Computers.
    {
        aoc::test::World wn = baseWorld();
        wn.gameState.player(PlayerId{0})
            ->cities()[0]
            ->stockpile()
            .addGoods(static_cast<uint16_t>(108), 5);
        const float withNeighbour = progressGained(wn, eco, dip, rng);
        CHECK(withNeighbour == doctest::Approx(base).epsilon(0.01f));
    }
}
