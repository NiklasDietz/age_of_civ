/**
 * @file test_great_people_names.cpp
 * @brief The 108-entry named Great Person roster had zero callers until
 *        recruitment started drawing from it (2026-09-04). These cases pin the
 *        roster's shape, the type-to-category bridge, and that a recruited
 *        person carries a historical name.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/simulation/greatpeople/GreatPeople.hpp"
#include "aoc/simulation/greatpeople/GreatPeopleExpanded.hpp"

#include <set>
#include <string>

using aoc::sim::GreatPersonCategory;
using aoc::sim::GreatPersonType;
using aoc::sim::NamedGreatPersonDef;

TEST_CASE("the roster holds 108 uniquely named people, twelve per category") {
    CHECK(aoc::sim::NAMED_GP_COUNT == 108);
    CHECK(aoc::sim::NAMED_GP_PER_CATEGORY == 12);
    const NamedGreatPersonDef* roster = aoc::sim::allNamedGreatPeople();
    REQUIRE(roster != nullptr);

    std::set<std::string> names;
    for (int32_t i = 0; i < aoc::sim::NAMED_GP_COUNT; ++i) {
        CHECK(roster[i].id == static_cast<uint8_t>(i));
        CHECK_FALSE(roster[i].name.empty());
        names.insert(std::string(roster[i].name));
    }
    CHECK(names.size() == static_cast<std::size_t>(aoc::sim::NAMED_GP_COUNT));
}

TEST_CASE("each category's twelve names are addressable and wrap") {
    for (uint8_t c = 0; c < static_cast<uint8_t>(GreatPersonCategory::Count); ++c) {
        const GreatPersonCategory cat = static_cast<GreatPersonCategory>(c);
        for (int32_t n = 0; n < aoc::sim::NAMED_GP_PER_CATEGORY; ++n) {
            const NamedGreatPersonDef& def = aoc::sim::namedGreatPersonForCategory(cat, n);
            CHECK(def.category == cat);
        }
        // Past the end it wraps rather than reading another category.
        CHECK(aoc::sim::namedGreatPersonForCategory(cat, aoc::sim::NAMED_GP_PER_CATEGORY).name ==
              aoc::sim::namedGreatPersonForCategory(cat, 0).name);
    }
}

TEST_CASE("every live great-person type maps onto its own roster category") {
    std::set<uint8_t> seen;
    for (uint8_t t = 0; t < static_cast<uint8_t>(GreatPersonType::Count); ++t) {
        const GreatPersonCategory cat =
            aoc::sim::categoryForGreatPersonType(static_cast<GreatPersonType>(t));
        CHECK(static_cast<uint8_t>(cat) < static_cast<uint8_t>(GreatPersonCategory::Count));
        seen.insert(static_cast<uint8_t>(cat));
    }
    CHECK(seen.size() == static_cast<std::size_t>(GreatPersonType::Count));
    // The cap per type is exactly the names available per category, so a full
    // run of recruitment uses each name once and never wraps.
    CHECK(aoc::sim::MAX_GP_PER_TYPE == aoc::sim::NAMED_GP_PER_CATEGORY);
}

TEST_CASE("a recruited great person carries a historical name") {
    aoc::test::World world = aoc::test::makeWorld(1);
    aoc::test::addCityAt(world, aoc::PlayerId{0}, 5, 5, "Home");
    aoc::game::Player& player = *world.gameState.players()[0];

    aoc::sim::PlayerGreatPeopleComponent& gp = player.greatPeople();
    const uint8_t scientist = static_cast<uint8_t>(GreatPersonType::Scientist);
    gp.points[scientist]    = gp.threshold(GreatPersonType::Scientist) + 1.0f;

    aoc::sim::checkGreatPeopleRecruitment(world.gameState, aoc::PlayerId{0});
    CHECK(gp.recruited[scientist] == 1);

    const aoc::game::Unit* recruited = nullptr;
    for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
        if (unit->greatPerson().owner != aoc::INVALID_PLAYER) {
            recruited = unit.get();
        }
    }
    REQUIRE(recruited != nullptr);

    const NamedGreatPersonDef& expected =
        aoc::sim::namedGreatPersonForCategory(GreatPersonCategory::Scientist, 0);
    CHECK(recruited->greatPerson().namedId == expected.id);
    CHECK(aoc::sim::namedGreatPersonDef(recruited->greatPerson().namedId).category ==
          GreatPersonCategory::Scientist);
    CHECK_FALSE(expected.name.empty());
}
