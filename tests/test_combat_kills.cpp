/**
 * @file test_combat_kills.cpp
 * @brief Kill-path pinning for combat: a killed defender must be removed
 *        from its owner exactly once, with the deferred-removal pattern
 *        keeping the caller's references valid for the duration of the
 *        call. Under the ASan debug preset these cases are live
 *        use-after-free detectors for both the melee and ranged paths.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/core/Random.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/simulation/unit/Combat.hpp"

namespace {

constexpr aoc::UnitTypeId WARRIOR{0};
constexpr aoc::UnitTypeId ARCHER{36};  // rangedStrength 25, range 2

struct CombatWorld {
    aoc::game::GameState gameState;
    aoc::map::HexGrid grid;
    aoc::Random rng{99u};

    CombatWorld() {
        gameState.initialize(2);
        grid.initialize(12, 12);
        for (int32_t i = 0; i < grid.tileCount(); ++i) {
            grid.setTerrain(i, aoc::map::TerrainType::Grassland);
        }
    }
};

} // namespace

TEST_CASE("ranged kill removes the defender; attacker survives untouched") {
    CombatWorld w;
    aoc::game::Player& p0 = *w.gameState.players()[0];
    aoc::game::Player& p1 = *w.gameState.players()[1];

    aoc::game::Unit& archer = p0.addUnit(ARCHER, {4, 4});
    aoc::game::Unit& target = p1.addUnit(WARRIOR, {5, 4});
    target.setHitPoints(1);
    REQUIRE(p1.units().size() == 1);

    aoc::sim::CombatResult r =
        aoc::sim::resolveRangedCombat(w.gameState, w.rng, w.grid, archer, target);
    // NOTE: `target` is dangling from here on -- the kill was drained
    // inside the call. Assert only via the owner and the result struct.

    CHECK(r.defenderKilled);
    CHECK(!r.attackerKilled);
    CHECK(p1.units().empty());
    REQUIRE(p0.units().size() == 1);
    CHECK(!archer.isDead());
}

TEST_CASE("melee kill removes the defender from its owner") {
    CombatWorld w;
    aoc::game::Player& p0 = *w.gameState.players()[0];
    aoc::game::Player& p1 = *w.gameState.players()[1];

    aoc::game::Unit& attacker = p0.addUnit(WARRIOR, {4, 4});
    aoc::game::Unit& target   = p1.addUnit(WARRIOR, {5, 4});
    target.setHitPoints(1);

    aoc::sim::CombatResult r =
        aoc::sim::resolveMeleeCombat(w.gameState, w.rng, w.grid, attacker, target);

    CHECK(r.defenderKilled);
    CHECK(p1.units().empty());
    REQUIRE(p0.units().size() == 1);
}

TEST_CASE("barbarian-style melee via Unit& overload actually deals damage") {
    // The EntityId overload with NULL_ENTITY returns an empty CombatResult;
    // BarbarianController used to call exactly that, making barbarian melee
    // a no-op. Pin that the Unit& overload (which it calls now) produces a
    // real result against a full-HP defender.
    CombatWorld w;
    aoc::game::Player& p0 = *w.gameState.players()[0];
    aoc::game::Player& p1 = *w.gameState.players()[1];

    aoc::game::Unit& attacker = p0.addUnit(WARRIOR, {4, 4});
    aoc::game::Unit& target   = p1.addUnit(WARRIOR, {5, 4});
    const int32_t hpBefore = target.hitPoints();

    aoc::sim::CombatResult r =
        aoc::sim::resolveMeleeCombat(w.gameState, w.rng, w.grid, attacker, target);

    CHECK(r.defenderDamage > 0);
    if (!r.defenderKilled) {
        CHECK(target.hitPoints() < hpBefore);
    }
}
