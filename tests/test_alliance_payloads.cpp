/**
 * @file test_alliance_payloads.cpp
 * @brief Alliance levels 2 and 3 pay the effects ALLIANCE_TYPE_DEFS designed.
 *        level2Bonus and level3Bonus had no readers at all: a generic +5% per
 *        level was applied to one yield per type, so choosing an alliance type
 *        changed only which yield the same bump landed on. goldMult and
 *        combatBonus on AllianceYieldModifiers had no readers either.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/simulation/diplomacy/AllianceTypes.hpp"
#include "aoc/simulation/diplomacy/DiplomacyExtensions.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"

using aoc::PlayerId;
using aoc::sim::ALLIANCE_TYPE_DEFS;
using aoc::sim::AllianceLevel;
using aoc::sim::AllianceType;
using aoc::sim::AllianceYieldModifiers;

namespace {

constexpr uint8_t SEATS = 2;

/// Player 0's modifiers with one active alliance of `type` at `level`.
[[nodiscard]] AllianceYieldModifiers modifiersFor(aoc::sim::DiplomacyManager& dip,
                                                  AllianceType type, AllianceLevel level) {
    aoc::sim::PairwiseRelation& rel = dip.relation(PlayerId{0}, PlayerId{1});
    for (aoc::sim::AllianceState& a : rel.alliances) {
        a = aoc::sim::AllianceState{};
    }
    aoc::sim::AllianceState& slot = rel.alliances[static_cast<std::size_t>(type)];
    slot.type                     = type;
    slot.level                    = level;
    slot.turnsActive              = 999;
    return aoc::sim::computeAllianceYieldModifiers(dip, PlayerId{0}, SEATS);
}

[[nodiscard]] const aoc::sim::AllianceTypeDef& defFor(AllianceType t) {
    return ALLIANCE_TYPE_DEFS[static_cast<std::size_t>(t) - 1];
}

} // namespace

TEST_CASE("no alliance means no modifiers") {
    aoc::sim::DiplomacyManager dip;
    dip.initialize(SEATS);
    const AllianceYieldModifiers m =
        aoc::sim::computeAllianceYieldModifiers(dip, PlayerId{0}, SEATS);
    CHECK(m.scienceMult == doctest::Approx(1.0f));
    CHECK(m.goldMult == doctest::Approx(1.0f));
    CHECK(m.combatBonus == doctest::Approx(0.0f));
    CHECK_FALSE(m.sharedVisibility);
    CHECK(m.sharedGreatWorkSlots == 0);
}

TEST_CASE("a Military alliance grants shared visibility at level 2, combat at level 3") {
    aoc::sim::DiplomacyManager dip;
    dip.initialize(SEATS);

    const AllianceYieldModifiers l1 =
        modifiersFor(dip, AllianceType::Military, AllianceLevel::Level1);
    CHECK_FALSE(l1.sharedVisibility);
    CHECK(l1.combatBonus == doctest::Approx(0.0f));

    const AllianceYieldModifiers l2 =
        modifiersFor(dip, AllianceType::Military, AllianceLevel::Level2);
    CHECK(l2.sharedVisibility); // the designed level-2 payload
    CHECK(l2.combatBonus == doctest::Approx(0.0f));

    const AllianceYieldModifiers l3 =
        modifiersFor(dip, AllianceType::Military, AllianceLevel::Level3);
    CHECK(l3.sharedVisibility);
    // The magnitude comes from the table, not a constant invented here.
    CHECK(l3.combatBonus == doctest::Approx(defFor(AllianceType::Military).level3Bonus.bonusValue));
}

TEST_CASE("an Economic alliance pays its designed trade gold at level 3") {
    aoc::sim::DiplomacyManager dip;
    dip.initialize(SEATS);

    const AllianceYieldModifiers l2 =
        modifiersFor(dip, AllianceType::Economic, AllianceLevel::Level2);
    const AllianceYieldModifiers l3 =
        modifiersFor(dip, AllianceType::Economic, AllianceLevel::Level3);

    CHECK(l3.goldMult > l2.goldMult);
    CHECK(l3.goldMult ==
          doctest::Approx(l2.goldMult + defFor(AllianceType::Economic).level3Bonus.bonusValue));
}

TEST_CASE("a Cultural alliance pays tourism at level 2 and shares slots at level 3") {
    aoc::sim::DiplomacyManager dip;
    dip.initialize(SEATS);

    const AllianceYieldModifiers l1 =
        modifiersFor(dip, AllianceType::Cultural, AllianceLevel::Level1);
    CHECK(l1.tourismMult == doctest::Approx(1.0f));
    CHECK(l1.sharedGreatWorkSlots == 0);

    const AllianceYieldModifiers l2 =
        modifiersFor(dip, AllianceType::Cultural, AllianceLevel::Level2);
    CHECK(l2.tourismMult ==
          doctest::Approx(1.0f + defFor(AllianceType::Cultural).level2Bonus.bonusValue));

    const AllianceYieldModifiers l3 =
        modifiersFor(dip, AllianceType::Cultural, AllianceLevel::Level3);
    CHECK(l3.sharedGreatWorkSlots > 0);
}

TEST_CASE("a Religious alliance pays its designed faith at level 2") {
    aoc::sim::DiplomacyManager dip;
    dip.initialize(SEATS);
    const AllianceYieldModifiers l2 =
        modifiersFor(dip, AllianceType::Religious, AllianceLevel::Level2);
    CHECK(l2.faithMult ==
          doctest::Approx(1.0f + defFor(AllianceType::Religious).level2Bonus.bonusValue));
}

TEST_CASE("each alliance type pays its own yield, not the same bump moved around") {
    // This is the point of the change: before, every type applied 0.05 per level
    // to a different field, so the type chosen never changed the magnitude.
    aoc::sim::DiplomacyManager dip;
    dip.initialize(SEATS);

    const AllianceYieldModifiers research =
        modifiersFor(dip, AllianceType::Research, AllianceLevel::Level3);
    const AllianceYieldModifiers economic =
        modifiersFor(dip, AllianceType::Economic, AllianceLevel::Level3);

    // A level-3 Research alliance's science edge and a level-3 Economic
    // alliance's gold edge are drawn from different table rows, so they differ.
    const float scienceEdge = research.scienceMult - 1.0f;
    const float goldEdge    = economic.goldMult - 1.0f;
    CHECK(scienceEdge != doctest::Approx(goldEdge));
}
