/**
 * @file test_air_sorties.cpp
 * @brief The air system's turn wiring: `resetAirSorties` restores sorties for
 *        aircraft only and puts fighters (never bombers) on patrol, the
 *        interceptor id list matches the unit table, and `processTurn` runs the
 *        reset for every seat. Until 2026-09-05 nothing called the reset, so an
 *        air unit that flew once never flew again and no fighter ever intercepted.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/core/Random.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/resource/EconomySimulation.hpp"
#include "aoc/simulation/turn/TurnProcessor.hpp"
#include "aoc/simulation/unit/CombatExtensions.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"

#include <string_view>

using aoc::PlayerId;
using aoc::UnitTypeId;

namespace {

constexpr UnitTypeId WARRIOR{0};
constexpr UnitTypeId FIGHTER{18};
constexpr UnitTypeId BOMBER{51};

/// A unit whose sorties are spent, with a stale patrol flag to prove the reset decides it.
aoc::game::Unit* addSpent(aoc::test::World& w, UnitTypeId type, int32_t q, int32_t r,
                          bool stalePatrol) {
    aoc::game::Unit* unit            = &aoc::test::addUnitAt(w, PlayerId{0}, type, q, r);
    unit->airUnit().sortiesRemaining = 0;
    unit->airUnit().isIntercepting   = stalePatrol;
    return unit;
}

} // namespace

TEST_CASE("resetAirSorties refuels aircraft only and puts fighters, not bombers, on patrol") {
    aoc::test::World w       = aoc::test::makeWorld(2);
    aoc::game::Unit* fighter = addSpent(w, FIGHTER, 5, 5, false);
    aoc::game::Unit* bomber  = addSpent(w, BOMBER, 6, 5, true);
    aoc::game::Unit* warrior = addSpent(w, WARRIOR, 7, 5, true);

    aoc::sim::resetAirSorties(w.gameState, PlayerId{0});

    CHECK(fighter->airUnit().sortiesRemaining == fighter->airUnit().maxSorties);
    CHECK(fighter->airUnit().isIntercepting);
    CHECK(bomber->airUnit().sortiesRemaining == bomber->airUnit().maxSorties);
    CHECK_FALSE(bomber->airUnit().isIntercepting);
    CHECK(warrior->airUnit().sortiesRemaining == 0); // not an aircraft: untouched
    CHECK(warrior->airUnit().isIntercepting);

    aoc::sim::resetAirSorties(w.gameState, PlayerId{9}); // unknown seat: no-op, no crash
}

TEST_CASE("the interceptor ids are exactly the fighter chain of the unit table") {
    for (const uint16_t id : {48, 18, 49, 50}) {
        const aoc::sim::UnitTypeDef& def = aoc::sim::unitTypeDef(UnitTypeId{id});
        CHECK(aoc::sim::isInterceptorType(UnitTypeId{id}));
        CHECK(def.unitClass == aoc::sim::UnitClass::Air);
        const bool fighterName =
            def.name.find("Fighter") != std::string_view::npos || def.name == "Biplane";
        CHECK_MESSAGE(fighterName, "id ", id, " is ", def.name);
    }
    for (const uint16_t id : {51, 52}) {
        CHECK_FALSE(aoc::sim::isInterceptorType(UnitTypeId{id}));
        CHECK(aoc::sim::unitTypeDef(UnitTypeId{id}).unitClass == aoc::sim::UnitClass::Air);
        CHECK(aoc::sim::unitTypeDef(UnitTypeId{id}).name.find("Bomber") != std::string_view::npos);
    }
    CHECK_FALSE(aoc::sim::isInterceptorType(WARRIOR));
}

TEST_CASE("processTurn refuels every seat's aircraft before anyone acts") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha");
    aoc::test::addCityAt(w, PlayerId{1}, 15, 9, "Beta");
    aoc::game::Unit* fighter                = addSpent(w, FIGHTER, 6, 5, false);
    aoc::game::Unit* rivalBomber            = &aoc::test::addUnitAt(w, PlayerId{1}, BOMBER, 16, 9);
    rivalBomber->airUnit().sortiesRemaining = 0;

    aoc::sim::EconomySimulation economy;
    aoc::sim::DiplomacyManager diplomacy;
    diplomacy.initialize(2);
    aoc::Random rng{3u};
    aoc::sim::TurnContext ctx;
    ctx.gameState   = &w.gameState;
    ctx.grid        = &w.grid;
    ctx.economy     = &economy;
    ctx.diplomacy   = &diplomacy;
    ctx.rng         = &rng;
    ctx.allPlayers  = {PlayerId{0}, PlayerId{1}};
    ctx.currentTurn = 1;

    aoc::sim::processTurn(ctx);

    CHECK(fighter->airUnit().sortiesRemaining == fighter->airUnit().maxSorties);
    CHECK(fighter->airUnit().isIntercepting);
    CHECK(rivalBomber->airUnit().sortiesRemaining == rivalBomber->airUnit().maxSorties);
    CHECK_FALSE(rivalBomber->airUnit().isIntercepting);
}
