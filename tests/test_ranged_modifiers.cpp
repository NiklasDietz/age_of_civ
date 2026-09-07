/**
 * @file test_ranged_modifiers.cpp
 * @brief Ranged combat gets the same civ, government and great-person bonuses
 *        as melee. The ranged branch had already been brought level on
 *        formation, promotion, flanking and fortification; these three families
 *        were the remainder, so an archer fought without its civ's combat
 *        bonus, its government's, or the general standing beside it.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/government/Government.hpp"
#include "aoc/simulation/government/GovernmentComponent.hpp"
#include "aoc/simulation/unit/Combat.hpp"

using aoc::PlayerId;
using aoc::UnitTypeId;

namespace {

constexpr UnitTypeId ARCHER{36}; // ranged
constexpr UnitTypeId WARRIOR{0}; // melee target

/// The civ whose flat combatStrengthBonus is largest, and that bonus.
[[nodiscard]] std::pair<aoc::sim::CivId, float> bestCombatCiv() {
    aoc::sim::CivId best{0};
    float bestBonus = 0.0f;
    for (uint8_t i = 0; i < aoc::sim::CIV_COUNT; ++i) {
        const aoc::sim::CivId id = static_cast<aoc::sim::CivId>(i);
        const float bonus        = aoc::sim::civDef(id).modifiers.combatStrengthBonus;
        if (bonus > bestBonus) {
            bestBonus = bonus;
            best      = id;
        }
    }
    return {best, bestBonus};
}

/// Attacker/defender strengths for a ranged attack by player 0 on player 1.
[[nodiscard]] aoc::sim::CombatStrengths rangedStrengths(aoc::test::World& w) {
    aoc::game::Unit& atk = *w.gameState.player(PlayerId{0})->units()[0];
    aoc::game::Unit& def = *w.gameState.player(PlayerId{1})->units()[0];
    return aoc::sim::computeCombatStrengths(w.gameState, w.grid, atk, def, true);
}

/// One archer for player 0 and one warrior for player 1, two tiles apart.
aoc::test::World archerVsWarrior() {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addUnitAt(w, PlayerId{0}, ARCHER, 5, 5);
    aoc::test::addUnitAt(w, PlayerId{1}, WARRIOR, 7, 5);
    return w;
}

} // namespace

TEST_CASE("the archer chosen for these tests really is ranged") {
    // If a content edit made this unit melee, every case below would silently
    // exercise the wrong branch.
    CHECK(aoc::sim::unitTypeDef(ARCHER).rangedStrength > 0);
    CHECK(aoc::sim::unitTypeDef(WARRIOR).rangedStrength == 0);
}

TEST_CASE("a civ's flat combat bonus reaches a ranged attacker") {
    const std::pair<aoc::sim::CivId, float> best = bestCombatCiv();
    if (best.second <= 0.0f) {
        return; // no civ in the table carries this bonus
    }

    aoc::test::World plain = archerVsWarrior();
    const float plainAtk   = rangedStrengths(plain).attack;

    aoc::test::World buffed = archerVsWarrior();
    buffed.gameState.player(PlayerId{0})->setCivId(best.first);
    const float buffedAtk = rangedStrengths(buffed).attack;

    CHECK(buffedAtk > plainAtk);
}

TEST_CASE("a government's combat bonus reaches a ranged attacker") {
    // Find a government whose modifiers carry a combat bonus.
    aoc::sim::GovernmentType withBonus = aoc::sim::GovernmentType::Chiefdom;
    float bonus                        = 0.0f;
    for (uint8_t i = 0; i < static_cast<uint8_t>(aoc::sim::GovernmentType::Count); ++i) {
        const aoc::sim::GovernmentType g = static_cast<aoc::sim::GovernmentType>(i);
        aoc::sim::PlayerGovernmentComponent probe{};
        probe.government = g;
        const float b    = aoc::sim::computeGovernmentModifiers(probe).combatStrengthBonus;
        if (b > bonus) {
            bonus     = b;
            withBonus = g;
        }
    }
    if (bonus <= 0.0f) {
        return; // no government in the table carries this bonus
    }

    aoc::test::World plain = archerVsWarrior();
    const float plainAtk   = rangedStrengths(plain).attack;

    aoc::test::World buffed                                    = archerVsWarrior();
    buffed.gameState.player(PlayerId{0})->government().government = withBonus;
    const float buffedAtk                                      = rangedStrengths(buffed).attack;

    CHECK(buffedAtk > plainAtk);
}

TEST_CASE("the bonus reaches a ranged defender too, not only the attacker") {
    const std::pair<aoc::sim::CivId, float> best = bestCombatCiv();
    if (best.second <= 0.0f) {
        return;
    }

    aoc::test::World plain = archerVsWarrior();
    const float plainDef   = rangedStrengths(plain).defense;

    aoc::test::World buffed = archerVsWarrior();
    buffed.gameState.player(PlayerId{1})->setCivId(best.first);
    const float buffedDef = rangedStrengths(buffed).defense;

    CHECK(buffedDef > plainDef);
}

TEST_CASE("melee still gets the same bonus -- the block was shared, not moved") {
    const std::pair<aoc::sim::CivId, float> best = bestCombatCiv();
    if (best.second <= 0.0f) {
        return;
    }
    aoc::test::World plain = aoc::test::makeWorld(2);
    aoc::test::addUnitAt(plain, PlayerId{0}, WARRIOR, 5, 5);
    aoc::test::addUnitAt(plain, PlayerId{1}, WARRIOR, 6, 5);
    const float plainAtk =
        aoc::sim::computeCombatStrengths(plain.gameState, plain.grid,
                                         *plain.gameState.player(PlayerId{0})->units()[0],
                                         *plain.gameState.player(PlayerId{1})->units()[0], false)
            .attack;

    aoc::test::World buffed = aoc::test::makeWorld(2);
    aoc::test::addUnitAt(buffed, PlayerId{0}, WARRIOR, 5, 5);
    aoc::test::addUnitAt(buffed, PlayerId{1}, WARRIOR, 6, 5);
    buffed.gameState.player(PlayerId{0})->setCivId(best.first);
    const float buffedAtk =
        aoc::sim::computeCombatStrengths(buffed.gameState, buffed.grid,
                                         *buffed.gameState.player(PlayerId{0})->units()[0],
                                         *buffed.gameState.player(PlayerId{1})->units()[0], false)
            .attack;

    CHECK(buffedAtk > plainAtk);
}
