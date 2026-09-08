/**
 * @file test_unit_upgrade_paths.cpp
 * @brief UNIT_TYPE_DEFS is the only table describing unit upgrades. It used to
 *        compete with an eight-row UPGRADE_PATHS list inside UnitUpgrade.cpp,
 *        which answered every query, so `upgradesTo` and `upgradeCost` on all
 *        78 rows were dead -- their only readers (Unit::upgradeTarget,
 *        canUpgrade, upgradeCost) had no callers at all. The two disagreed:
 *        the unit table sent a Slinger to the Archer, UPGRADE_PATHS to the
 *        Crossbowman.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/Unit.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/unit/UnitUpgrade.hpp"

#include <set>

using aoc::UnitTypeId;
using aoc::sim::UNIT_TYPE_COUNT;
using aoc::sim::UNIT_TYPE_DEFS;
using aoc::sim::unitTypeDef;

TEST_CASE("every unit whose upgradesTo is set can actually be upgraded") {
    int32_t withSuccessor = 0;
    // Iterate the ROWS, not 0..count as ids. The unit id space is sparse -- 78
    // rows with ids running to 102 -- and unitTypeDef falls back silently to the
    // Warrior for an id that does not exist, so an index loop asserted the
    // Warrior's properties six times over and never reached the six units
    // numbered above the row count.
    for (const aoc::sim::UnitTypeDef& row : UNIT_TYPE_DEFS) {
        const UnitTypeId id   = row.id;
        const UnitTypeId next = row.upgradesTo;
        if (!next.isValid()) {
            continue;
        }
        ++withSuccessor;

        // The successor must be a real row, which in a sparse id space means a
        // row actually carrying that id, not merely an id below the row count.
        CHECK(unitTypeDef(next).id == next);
        // And the upgrade path must be reachable through the live query, which
        // is the whole point: the column used to name a successor nobody asked.
        const std::vector<aoc::sim::UnitUpgradeDef> paths = aoc::sim::getAvailableUpgrades(id);
        REQUIRE(paths.size() == 1);
        CHECK(paths[0].from == id);
        CHECK(paths[0].to == next);
    }
    // Guard against the table being emptied by a bad edit.
    CHECK(withSuccessor > 30);
}

TEST_CASE("a unit with no successor offers no upgrade") {
    for (const aoc::sim::UnitTypeDef& row : UNIT_TYPE_DEFS) {
        if (row.upgradesTo.isValid()) {
            continue;
        }
        CHECK(aoc::sim::getAvailableUpgrades(row.id).empty());
    }
}

TEST_CASE("no two rows claim the same upgrade, and no chain loops") {
    // One source names one successor, so a duplicate would mean two rows
    // describing the same step -- the exact drift that let the old second
    // table disagree with this one.
    std::set<uint16_t> seenSources;
    for (const aoc::sim::UnitTypeDef& row : UNIT_TYPE_DEFS) {
        if (!row.upgradesTo.isValid()) {
            continue;
        }
        CHECK(seenSources.insert(row.id.value).second);
    }

    // Walking any chain must terminate. A cycle would hang the upgrade UI.
    for (const aoc::sim::UnitTypeDef& start : UNIT_TYPE_DEFS) {
        UnitTypeId cur = start.id;
        std::set<uint16_t> visited{cur.value};
        int32_t steps = 0;
        while (unitTypeDef(cur).upgradesTo.isValid() && steps < UNIT_TYPE_COUNT + 1) {
            cur = unitTypeDef(cur).upgradesTo;
            REQUIRE(unitTypeDef(cur).id == cur); // a row really carries that id
            CHECK(visited.insert(cur.value).second); // revisiting means a cycle
            ++steps;
        }
        CHECK(steps <= UNIT_TYPE_COUNT);
    }
}

TEST_CASE("a unit never upgrades into itself or backwards in era") {
    for (const aoc::sim::UnitTypeDef& def : UNIT_TYPE_DEFS) {
        if (!def.upgradesTo.isValid()) {
            continue;
        }
        CHECK(def.upgradesTo != def.id);
        const aoc::sim::UnitTypeDef& next = unitTypeDef(def.upgradesTo);
        // Same era is allowed (a side-grade within an age); going back is not.
        CHECK(static_cast<int32_t>(next.era) >= static_cast<int32_t>(def.era));
    }
}

TEST_CASE("the tech gate is the successor's own requirement") {
    // A separate tech column per path is what let the two tables drift. A unit
    // you cannot build yet is a unit you cannot upgrade into.
    for (const aoc::sim::UnitTypeDef& row : UNIT_TYPE_DEFS) {
        const std::vector<aoc::sim::UnitUpgradeDef> paths = aoc::sim::getAvailableUpgrades(row.id);
        if (paths.empty()) {
            continue;
        }
        CHECK(paths[0].requiredTech == unitTypeDef(paths[0].to).requiredTech);
    }
}

TEST_CASE("the horse line reaches armour instead of dead-ending") {
    // Horseman -> Knight -> Cuirassier -> Cavalry had no step past Cavalry, so
    // that whole line and the Heavy Chariot feeding it stopped at Industrial.
    constexpr UnitTypeId CAVALRY{14};
    REQUIRE(unitTypeDef(CAVALRY).upgradesTo.isValid());
    CHECK(aoc::sim::getAvailableUpgrades(CAVALRY).size() == 1);
}
