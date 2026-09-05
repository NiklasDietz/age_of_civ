/**
 * @file test_combat_preview.cpp
 * @brief The combat preview shares the resolution's strength computation:
 *        its expected damage is the resolution's formula at random factor 1.0,
 *        so promotions, formations and fortification move both the same way,
 *        and the actual roll lands inside the preview's 0.8 to 1.2 band.
 *        Until 2026-09-05 previewCombat had no caller and lacked promotions,
 *        formations and the civ/government bonuses (Civ VI plan Phase 2.5).
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/core/Random.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/unit/Combat.hpp"
#include "aoc/simulation/unit/Promotion.hpp"

#include <algorithm>
#include <cmath>

using aoc::PlayerId;
using aoc::PromotionId;
using aoc::UnitTypeId;
using aoc::hex::AxialCoord;

namespace {
constexpr UnitTypeId WARRIOR{0};
constexpr UnitTypeId ARCHER{36};

int32_t expectedDamage(float atk, float def) {
    const float a = std::clamp(atk, 0.01f, 1000.0f);
    const float d = std::clamp(def, 0.01f, 1000.0f);
    return std::clamp(static_cast<int32_t>(30.0f * (a / d)), 0, 100);
}
} // namespace

TEST_CASE("the preview is the shared strength formula at random factor 1.0") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::game::Unit& attacker = aoc::test::addUnitAt(w, PlayerId{0}, WARRIOR, 5, 5);
    aoc::game::Unit& defender = aoc::test::addUnitAt(w, PlayerId{1}, WARRIOR, 6, 5);

    const aoc::sim::CombatStrengths st =
        aoc::sim::computeCombatStrengths(w.gameState, w.grid, attacker, defender, false);
    const aoc::sim::CombatPreview pv = aoc::sim::previewCombat(w.gameState, w.grid, attacker, defender);
    CHECK(pv.expectedDefenderDamage == expectedDamage(st.attack, st.defense) * 8 / 10);
    CHECK(pv.expectedAttackerDamage == expectedDamage(st.defense, st.attack) * 8 / 10);
}

TEST_CASE("promotions, fortification and formations move the preview like the resolution") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::game::Unit& attacker = aoc::test::addUnitAt(w, PlayerId{0}, WARRIOR, 5, 5);
    aoc::game::Unit& defender = aoc::test::addUnitAt(w, PlayerId{1}, WARRIOR, 6, 5);
    const aoc::sim::CombatPreview base = aoc::sim::previewCombat(w.gameState, w.grid, attacker, defender);

    const float baseAttack =
        aoc::sim::computeCombatStrengths(w.gameState, w.grid, attacker, defender, false).attack;
    attacker.experience().promotions.push_back(PromotionId{0});   // Battlecry +3
    const aoc::sim::CombatPreview promoted = aoc::sim::previewCombat(w.gameState, w.grid, attacker, defender);
    CHECK(promoted.expectedDefenderDamage > base.expectedDefenderDamage);
    CHECK(aoc::sim::computeCombatStrengths(w.gameState, w.grid, attacker, defender, false).attack
          == doctest::Approx(baseAttack + 3.0f));

    defender.setState(aoc::sim::UnitState::Fortified);
    const aoc::sim::CombatPreview fortified = aoc::sim::previewCombat(w.gameState, w.grid, attacker, defender);
    CHECK(fortified.expectedDefenderDamage < promoted.expectedDefenderDamage);
}

TEST_CASE("a melee roll lands inside the preview's band; a ranged preview takes no damage") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::game::Unit& attacker = aoc::test::addUnitAt(w, PlayerId{0}, WARRIOR, 5, 5);
    aoc::game::Unit& defender = aoc::test::addUnitAt(w, PlayerId{1}, WARRIOR, 6, 5);
    const aoc::sim::CombatPreview pv = aoc::sim::previewCombat(w.gameState, w.grid, attacker, defender);
    aoc::Random rng{3};
    const aoc::sim::CombatResult r = aoc::sim::resolveMeleeCombat(w.gameState, rng, w.grid, attacker, defender);
    CHECK(r.defenderDamage >= static_cast<int32_t>(pv.expectedDefenderDamage * 0.8f) - 1);
    CHECK(r.defenderDamage <= static_cast<int32_t>(pv.expectedDefenderDamage * 1.2f) + 1);

    aoc::game::Unit& archer = aoc::test::addUnitAt(w, PlayerId{0}, ARCHER, 8, 8);
    aoc::game::Unit& target = aoc::test::addUnitAt(w, PlayerId{1}, WARRIOR, 9, 8);
    const aoc::sim::CombatPreview rp = aoc::sim::previewCombat(w.gameState, w.grid, archer, target);
    CHECK(rp.expectedAttackerDamage == 0);
    CHECK(rp.expectedDefenderDamage > 0);
}
