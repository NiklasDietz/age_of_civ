/**
 * @file test_supply_healing.cpp
 * @brief A unit out of supply cannot heal. SupplyLines.hpp has promised this
 *        since it was written, but the heal block in TurnProcessor never
 *        consulted supply(), so a cut-off unit healed as if nothing were wrong
 *        -- often out-healing the -10 HP attrition meant to grind it down.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/resource/EconomySimulation.hpp"
#include "aoc/simulation/turn/TurnProcessor.hpp"
#include "aoc/simulation/unit/SupplyLines.hpp"
#include "aoc/core/Random.hpp"

using aoc::PlayerId;
using aoc::UnitTypeId;

namespace {

constexpr UnitTypeId WARRIOR{0};

/// A damaged warrior for player 0 at (q, r).
aoc::game::Unit& damagedWarrior(aoc::test::World& w, int32_t q, int32_t r, int32_t hp) {
    aoc::game::Unit& u = aoc::test::addUnitAt(w, PlayerId{0}, WARRIOR, q, r);
    u.setHitPoints(hp);
    return u;
}

/// One player turn. computeSupplyLines runs inside it and would recompute the
/// supply flag, so these tests assert on the heal rule via the flag the turn
/// itself derives, not one forced from outside.
void runTurn(aoc::test::World& w, aoc::sim::EconomySimulation& eco, aoc::sim::DiplomacyManager& dip,
             aoc::Random& rng) {
    aoc::sim::TurnContext ctx;
    ctx.gameState   = &w.gameState;
    ctx.grid        = &w.grid;
    ctx.economy     = &eco;
    ctx.diplomacy   = &dip;
    ctx.rng         = &rng;
    ctx.allPlayers  = {PlayerId{0}};
    ctx.currentTurn = 1;
    aoc::sim::processPlayerTurn(ctx, PlayerId{0});
}

} // namespace

TEST_CASE("the supply component defaults to supplied") {
    aoc::sim::UnitSupplyComponent s;
    CHECK(s.isSupplied);
}

TEST_CASE("an unsupplied unit does not heal; a supplied one does") {
    // Both runs are identical but for where the warrior stands. Supply is
    // recomputed inside the turn -- a flag forced from outside is overwritten --
    // so the unit must be genuinely out of range of a supply source: a city or
    // an owned fort, reached over at most a few tiles without a road.
    constexpr int32_t START_HP = 50;

    aoc::sim::EconomySimulation eco;
    aoc::sim::DiplomacyManager dip;
    dip.initialize(1);
    aoc::Random rng{7u};

    int32_t suppliedHp   = 0;
    int32_t unsuppliedHp = 0;
    bool suppliedFlag    = false;
    bool unsuppliedFlag  = true;

    {
        aoc::test::World w = aoc::test::makeWorld(1);
        aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha");
        aoc::game::Unit& u = damagedWarrior(w, 5, 5, START_HP); // in the city
        runTurn(w, eco, dip, rng);
        suppliedHp   = u.hitPoints();
        suppliedFlag = u.supply().isSupplied;
    }
    {
        aoc::test::World w = aoc::test::makeWorld(1);
        aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha");
        aoc::game::Unit& u = damagedWarrior(w, 20, 14, START_HP); // far away
        runTurn(w, eco, dip, rng);
        unsuppliedHp   = u.hitPoints();
        unsuppliedFlag = u.supply().isSupplied;
    }

    // The turn derived the two supply states we were after.
    REQUIRE(suppliedFlag);
    REQUIRE_FALSE(unsuppliedFlag);

    // The supplied unit healed.
    CHECK(suppliedHp > START_HP);

    // The cut-off one lost exactly the attrition and gained nothing. This has to
    // be an equality: when healing wrongly ran for it too, the +5 of neutral
    // territory against the -10 of attrition still left it below where it
    // started, so any "lost some hit points" assertion passes either way and
    // proves nothing.
    CHECK(unsuppliedHp == START_HP - aoc::sim::UNSUPPLIED_ATTRITION_HP);
    CHECK(unsuppliedHp < suppliedHp);
}

TEST_CASE("a unit at full health is untouched either way") {
    aoc::sim::EconomySimulation eco;
    aoc::sim::DiplomacyManager dip;
    dip.initialize(1);
    aoc::Random rng{7u};

    aoc::test::World w = aoc::test::makeWorld(1);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha");
    aoc::game::Unit& u  = aoc::test::addUnitAt(w, PlayerId{0}, WARRIOR, 5, 5);
    const int32_t maxHp = u.typeDef().maxHitPoints;
    u.setHitPoints(maxHp);

    runTurn(w, eco, dip, rng);
    CHECK(u.hitPoints() <= maxHp); // never healed past the cap
}
