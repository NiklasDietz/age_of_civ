/**
 * @file test_promotions.cpp
 * @brief Promotion trees: the append-only table with class masks and prerequisites,
 *        the availability filter, the scored deterministic pick per class, and the
 *        turn pass promoting human units too (they never promoted until 2026-09-05).
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/unit/Promotion.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"

#include <algorithm>

using aoc::PlayerId;
using aoc::PromotionId;
using aoc::sim::availablePromotions;
using aoc::sim::PROMOTION_DEFS;
using aoc::sim::UnitClass;
using aoc::sim::UnitExperienceComponent;

namespace {

constexpr aoc::UnitTypeId WARRIOR{0}; // Melee
constexpr aoc::UnitTypeId ARCHER{36}; // Ranged

bool contains(const std::vector<PromotionId>& v, uint16_t id) {
    return std::find(v.begin(), v.end(), PromotionId{id}) != v.end();
}

} // namespace

TEST_CASE("the table is append-only, roots are open to every class, tier two is gated") {
    REQUIRE(PROMOTION_DEFS.size() == 12);
    for (std::size_t i = 0; i < PROMOTION_DEFS.size(); ++i) {
        CHECK(PROMOTION_DEFS[i].id.value == i);
    }
    for (std::size_t i = 0; i < 6; ++i) {
        CHECK(PROMOTION_DEFS[i].classMask == aoc::sim::PROMO_ANY_CLASS);
        CHECK_FALSE(PROMOTION_DEFS[i].prerequisite.isValid());
    }
    for (std::size_t i = 6; i < 12; ++i) {
        CHECK(PROMOTION_DEFS[i].prerequisite.isValid());
        CHECK(PROMOTION_DEFS[i].prerequisite.value < 6);
    }
    CHECK(PROMOTION_DEFS[6].classMask == aoc::sim::PROMO_MELEE_LINE);
    CHECK(PROMOTION_DEFS[7].classMask == aoc::sim::PROMO_RANGED_LINE);
    CHECK((PROMOTION_DEFS[8].classMask & aoc::sim::promotionClassBit(UnitClass::Cavalry)) != 0);
    CHECK((PROMOTION_DEFS[8].classMask & aoc::sim::promotionClassBit(UnitClass::Melee)) == 0);
}

TEST_CASE("availability filters held promotions, the class line and the prerequisite") {
    UnitExperienceComponent xp;
    std::vector<PromotionId> melee = availablePromotions(xp, UnitClass::Melee);
    CHECK(melee.size() == 6); // the six roots only
    CHECK_FALSE(contains(melee, 6));

    xp.promotions.push_back(PromotionId{0}); // Battlecry held
    melee = availablePromotions(xp, UnitClass::Melee);
    CHECK_FALSE(contains(melee, 0));
    CHECK(contains(melee, 6)); // Zweihander opens for the melee line
    CHECK_FALSE(contains(availablePromotions(xp, UnitClass::Ranged), 6)); // not for ranged
    CHECK_FALSE(contains(melee, 7)); // Camouflage needs Tortoise and the ranged line

    xp.promotions.push_back(PromotionId{3});                        // Medic held
    CHECK(contains(availablePromotions(xp, UnitClass::Ranged), 9)); // Survivalism: any class
    CHECK(contains(availablePromotions(xp, UnitClass::Melee), 9));
}

TEST_CASE("the scored pick is deterministic and class-shaped") {
    UnitExperienceComponent fresh;
    CHECK(aoc::sim::aiSelectPromotion(fresh, UnitClass::Melee) ==
          PromotionId{5}); // Elite: 5 combat + 5 heal + 0.1 terrain
    CHECK(aoc::sim::aiSelectPromotion(fresh, UnitClass::Ranged) ==
          PromotionId{5}); // Elite still edges Tortoise (0.15 * 25 = 3.75)
    CHECK(aoc::sim::aiSelectPromotion(fresh, UnitClass::Cavalry) ==
          PromotionId{5}); // Elite 12.5 vs Blitz 8

    UnitExperienceComponent veteran;
    veteran.promotions = {PromotionId{5}, PromotionId{0}}; // Elite and Battlecry held
    CHECK(aoc::sim::aiSelectPromotion(veteran, UnitClass::Melee) == PromotionId{11}); // Legendary 15.5 edges Zweihander 15
    veteran.promotions.push_back(PromotionId{11});
    CHECK(aoc::sim::aiSelectPromotion(veteran, UnitClass::Melee) == PromotionId{6}); // then Zweihander 15
    CHECK(aoc::sim::aiSelectPromotion(veteran, UnitClass::Cavalry) == PromotionId{4}); // Legendary held: Blitz 8 beats Medic 5
    UnitExperienceComponent rider;
    rider.promotions = {PromotionId{2}}; // Commando held
    CHECK(aoc::sim::aiSelectPromotion(rider, UnitClass::Cavalry) ==
          PromotionId{5}); // Elite 12.5 beats Depredation 8
    rider.promotions.push_back(PromotionId{5});
    rider.promotions.push_back(PromotionId{11});
    CHECK(aoc::sim::aiSelectPromotion(rider, UnitClass::Cavalry) == PromotionId{4}); // Blitz 8 ties Depredation 8: lower id wins

    // Ties go to the lower id, so appended rows never reorder existing picks.
    CHECK(aoc::sim::scorePromotion(PROMOTION_DEFS[9], UnitClass::Melee) ==
          doctest::Approx(0.5f * 10 + 10.0f * 0.05f));
}

TEST_CASE("processUnitPromotions promotes human and AI units alike and consumes the XP") {
    aoc::test::World w            = aoc::test::makeWorld(2);
    aoc::game::Unit& human        = aoc::test::addUnitAt(w, PlayerId{0}, WARRIOR, 5, 5);
    aoc::game::Unit& ai           = aoc::test::addUnitAt(w, PlayerId{1}, ARCHER, 10, 5);
    human.experience().experience = 20; // first threshold is 15
    ai.experience().experience    = 20;

    aoc::sim::processUnitPromotions(*w.gameState.player(PlayerId{0}), true);
    aoc::sim::processUnitPromotions(*w.gameState.player(PlayerId{1}), false);
    REQUIRE(human.experience().promotions.size() == 1);
    CHECK(human.experience().promotions[0] == PromotionId{5});
    CHECK(human.experience().level == 1);
    CHECK(human.experience().experience == 5); // 20 - 15
    REQUIRE(ai.experience().promotions.size() == 1);
    CHECK(ai.experience().level == 1);

    // Not enough XP for the next level: nothing happens.
    aoc::sim::processUnitPromotions(*w.gameState.player(PlayerId{0}), true);
    CHECK(human.experience().level == 1);

    // Six levels exhaust the table for a class with nothing left: no crash, no loop.
    human.experience().experience = 100000;
    for (int32_t i = 0; i < 8; ++i) {
        aoc::sim::processUnitPromotions(*w.gameState.player(PlayerId{0}), true);
    }
    CHECK(human.experience().level == 6);
    CHECK(human.experience().promotions.size() == 6);
}
