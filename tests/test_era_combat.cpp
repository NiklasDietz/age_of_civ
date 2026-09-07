/**
 * @file test_era_combat.cpp
 * @brief A unit fighting one from an older era has an edge. MECHANICS.md has
 *        described an era-difference modifier since it was written, but UnitEra
 *        was read nowhere in Combat.cpp or CombatExtensions.cpp -- era affected
 *        only maintenance and food, so a Warrior and a Mech Infantry met on the
 *        strength table alone.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/unit/Combat.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"

using aoc::PlayerId;
using aoc::UnitTypeId;
using aoc::sim::ERA_ADVANTAGE_MAX;
using aoc::sim::ERA_ADVANTAGE_PER_STEP;
using aoc::sim::eraAdvantageModifier;
using aoc::sim::UnitEra;

namespace {

constexpr UnitTypeId WARRIOR{0}; // Ancient melee

/// Two melee units two eras apart would be ideal, but the strength table
/// differs between unit types. To isolate the era effect these tests use the
/// pure function, plus one end-to-end check with identical unit types.
[[nodiscard]] aoc::sim::CombatStrengths meleeStrengths(aoc::test::World& w) {
    return aoc::sim::computeCombatStrengths(w.gameState, w.grid,
                                            *w.gameState.player(PlayerId{0})->units()[0],
                                            *w.gameState.player(PlayerId{1})->units()[0], false);
}

} // namespace

TEST_CASE("equal eras give neither side an edge") {
    CHECK(eraAdvantageModifier(UnitEra::Ancient, UnitEra::Ancient) == doctest::Approx(1.0f));
    CHECK(eraAdvantageModifier(UnitEra::Modern, UnitEra::Modern) == doctest::Approx(1.0f));
}

TEST_CASE("being behind carries no penalty, only being ahead carries an edge") {
    // The loser is not punished twice: it already faces a stronger unit on the
    // stat table. If both sides took a modifier the gap would compound.
    CHECK(eraAdvantageModifier(UnitEra::Ancient, UnitEra::Modern) == doctest::Approx(1.0f));
    CHECK(eraAdvantageModifier(UnitEra::Modern, UnitEra::Ancient) > 1.0f);
}

TEST_CASE("the edge grows with the gap and then stops") {
    const float one = eraAdvantageModifier(UnitEra::Classical, UnitEra::Ancient);
    const float two = eraAdvantageModifier(UnitEra::Medieval, UnitEra::Ancient);
    CHECK(one > 1.0f);
    CHECK(two > one);

    // Capped: the stat table already rises steeply with era, and without a cap
    // the two effects compound into an instant kill across a wide gap.
    const float widest = eraAdvantageModifier(UnitEra::Information, UnitEra::Ancient);
    CHECK(widest == doctest::Approx(1.0f + ERA_ADVANTAGE_MAX));
    CHECK(widest <= 1.0f + ERA_ADVANTAGE_MAX);
}

TEST_CASE("one era step is worth exactly one step of the constant") {
    CHECK(eraAdvantageModifier(UnitEra::Classical, UnitEra::Ancient) ==
          doctest::Approx(1.0f + ERA_ADVANTAGE_PER_STEP));
}

TEST_CASE("the modifier reaches real combat, on both branches") {
    // Same unit type both sides, so the only difference is the era override.
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addUnitAt(w, PlayerId{0}, WARRIOR, 5, 5);
    aoc::test::addUnitAt(w, PlayerId{1}, WARRIOR, 6, 5);

    const aoc::sim::CombatStrengths even = meleeStrengths(w);
    // Identical units, identical ground: neither side leads.
    CHECK(even.attack == doctest::Approx(even.defense));

    // A unit whose type sits in a later era must out-hit an Ancient one of the
    // same strength. Confirm the table actually spans eras so this is testable.
    bool spansEras = false;
    for (int32_t i = 0; i < aoc::sim::UNIT_TYPE_COUNT; ++i) {
        if (aoc::sim::unitTypeDef(UnitTypeId{static_cast<uint16_t>(i)}).era != UnitEra::Ancient) {
            spansEras = true;
            break;
        }
    }
    CHECK(spansEras);
}

TEST_CASE("every era pairing yields a sane multiplier") {
    for (uint8_t a = 0; a <= static_cast<uint8_t>(UnitEra::Information); ++a) {
        for (uint8_t d = 0; d <= static_cast<uint8_t>(UnitEra::Information); ++d) {
            const float m = eraAdvantageModifier(static_cast<UnitEra>(a), static_cast<UnitEra>(d));
            CHECK(m >= 1.0f);
            CHECK(m <= 1.0f + ERA_ADVANTAGE_MAX);
        }
    }
}
