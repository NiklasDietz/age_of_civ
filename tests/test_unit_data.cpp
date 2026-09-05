/**
 * @file test_unit_data.cpp
 * @brief Unit table integrity: no phantom rows, every upgrade target exists,
 *        city-states queue an era-appropriate defender (not a Settler),
 *        production validation follows the sparse id space, and aircraft are
 *        born with the range of their row. Found 2026-09-05 (Civ VI plan, defect 6).
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/debug/GameControlValidation.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/citystate/CityState.hpp"
#include "aoc/simulation/unit/CombatExtensions.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/unit/UnitUpgrade.hpp"

#include <set>

using aoc::PlayerId;
using aoc::UnitTypeId;

TEST_CASE("every unit row has a name and a unique id; the table has no phantom rows") {
    std::set<uint16_t> ids;
    for (const aoc::sim::UnitTypeDef& def : aoc::sim::UNIT_TYPE_DEFS) {
        CHECK_FALSE(def.name.empty());
        CHECK(def.maxHitPoints > 0);
        CHECK(ids.insert(def.id.value).second);
    }
    CHECK(ids.size() == aoc::sim::UNIT_TYPE_DEFS.size());
}

TEST_CASE("every upgrade path ends at a real unit") {
    for (const aoc::sim::UnitTypeDef& def : aoc::sim::UNIT_TYPE_DEFS) {
        for (const aoc::sim::UnitUpgradeDef& up : aoc::sim::getAvailableUpgrades(def.id)) {
            CHECK(aoc::sim::unitTypeDef(up.to).id == up.to);
        }
    }
    const std::vector<aoc::sim::UnitUpgradeDef> spear = aoc::sim::getAvailableUpgrades(UnitTypeId{9});
    REQUIRE(spear.size() == 1);
    CHECK(spear[0].to == UnitTypeId{46}); // Pike and Shot
}

TEST_CASE("a city-state fields the strongest defender of the world's era") {
    CHECK(aoc::sim::cityStateDefenderFor(0) == UnitTypeId{0});  // Warrior
    CHECK(aoc::sim::cityStateDefenderFor(1) == UnitTypeId{10}); // Swordsman
    CHECK(aoc::sim::cityStateDefenderFor(3) == UnitTypeId{34}); // Musketman
    CHECK(aoc::sim::cityStateDefenderFor(7) == UnitTypeId{35}); // Mech Infantry
    for (uint8_t era = 0; era <= 7; ++era) {
        const aoc::sim::UnitTypeDef& def = aoc::sim::unitTypeDef(aoc::sim::cityStateDefenderFor(era));
        CHECK(aoc::sim::isMilitary(def.unitClass));
        CHECK(static_cast<uint8_t>(def.era) <= era);
    }
}

TEST_CASE("production validation follows the sparse unit id space") {
    CHECK(aoc::debug::isProductionItemValid(aoc::sim::ProductionItemType::Unit, 0));
    CHECK(aoc::debug::isProductionItemValid(aoc::sim::ProductionItemType::Unit, 100)); // Spy
    CHECK(aoc::debug::isProductionItemValid(aoc::sim::ProductionItemType::Unit, 102));
    CHECK_FALSE(aoc::debug::isProductionItemValid(aoc::sim::ProductionItemType::Unit, 13));
    CHECK_FALSE(aoc::debug::isProductionItemValid(aoc::sim::ProductionItemType::Unit, 81));
    CHECK_FALSE(aoc::debug::isProductionItemValid(aoc::sim::ProductionItemType::Unit, -1));
}

TEST_CASE("a new aircraft flies the range of its row") {
    aoc::test::World w = aoc::test::makeWorld(2);
    const UnitTypeId fighter{18};
    aoc::game::Unit& u = aoc::test::addUnitAt(w, PlayerId{0}, fighter, 5, 5);
    REQUIRE(aoc::sim::isAirUnit(u.typeDef().unitClass));
    CHECK(u.airUnit().operationalRange == aoc::sim::unitTypeDef(fighter).range);
    aoc::game::Unit& warrior = aoc::test::addUnitAt(w, PlayerId{0}, UnitTypeId{0}, 6, 6);
    CHECK(warrior.airUnit().operationalRange == 8); // untouched default for land units
}
