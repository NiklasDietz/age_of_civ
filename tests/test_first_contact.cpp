/**
 * @file test_first_contact.cpp
 * @brief First contact runs in the turn loop for both front ends, grievances lower
 *        the relation score, and a war declaration fans obligations out to the
 *        target's allies through the installed tracker. Until 2026-09-05 only the
 *        headless tool scanned for contact, the grievance gate compared a negative
 *        total against > 0 and never fired, and no declareWar caller passed a tracker.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/Player.hpp"
#include "aoc/simulation/diplomacy/AllianceObligations.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/diplomacy/Grievance.hpp"
#include "aoc/simulation/turn/TurnEventLog.hpp"

using aoc::PlayerId;
using aoc::UnitTypeId;
using aoc::sim::DiplomacyManager;
using aoc::sim::TurnEventLog;
using aoc::sim::TurnEventType;

namespace {

int32_t countEvents(const TurnEventLog& log, TurnEventType type) {
    int32_t n = 0;
    for (const aoc::sim::TurnEvent& e : log.events()) {
        if (e.type == type) {
            ++n;
        }
    }
    return n;
}

const aoc::sim::RelationModifier* grievanceModifier(const DiplomacyManager& d, PlayerId a,
                                                   PlayerId b) {
    for (const aoc::sim::RelationModifier& m : d.relation(a, b).modifiers) {
        if (m.reason == "Grievances") {
            return &m;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("two seats meet when a unit of one is within three tiles of a unit or city of the other") {
    aoc::test::World w = aoc::test::makeWorld(3);
    DiplomacyManager d;
    d.initialize(3);
    TurnEventLog log;
    aoc::test::addUnitAt(w, PlayerId{0}, UnitTypeId{0}, 5, 5);
    aoc::test::addUnitAt(w, PlayerId{1}, UnitTypeId{0}, 8, 5); // distance 3: in sight
    aoc::test::addCityAt(w, PlayerId{2}, 20, 12, "Far");

    aoc::sim::processFirstContact(w.gameState, w.grid, d, &log, 7);
    CHECK(d.haveMet(PlayerId{0}, PlayerId{1}));
    CHECK_FALSE(d.haveMet(PlayerId{0}, PlayerId{2}));
    CHECK_FALSE(d.haveMet(PlayerId{1}, PlayerId{2}));
    CHECK(d.relation(PlayerId{0}, PlayerId{1}).metOnTurn == 7);
    CHECK(d.relation(PlayerId{1}, PlayerId{0}).hasMet);
    CHECK(countEvents(log, TurnEventType::PlayersMet) == 1);

    // A second scan records nothing new for an already-met pair.
    aoc::sim::processFirstContact(w.gameState, w.grid, d, &log, 8);
    CHECK(countEvents(log, TurnEventType::PlayersMet) == 1);

    // A unit next to a city meets its owner.
    aoc::test::addUnitAt(w, PlayerId{0}, UnitTypeId{0}, 19, 12);
    aoc::sim::processFirstContact(w.gameState, w.grid, d, &log, 9);
    CHECK(d.haveMet(PlayerId{0}, PlayerId{2}));
    CHECK(d.relation(PlayerId{0}, PlayerId{2}).metOnTurn == 9);
    CHECK(countEvents(log, TurnEventType::PlayersMet) == 2);
}

TEST_CASE("grievances lower the relation score by half their magnitude, capped at 40") {
    aoc::test::World w = aoc::test::makeWorld(2);
    DiplomacyManager d;
    d.initialize(2);
    aoc::game::Player& holder = *w.gameState.player(PlayerId{0});

    aoc::sim::applyGrievanceModifiers(w.gameState, d);
    CHECK(grievanceModifier(d, PlayerId{0}, PlayerId{1}) == nullptr);

    holder.grievances().addGrievance(aoc::sim::GrievanceType::BrokePromise, PlayerId{1}); // -20
    holder.grievances().addGrievance(aoc::sim::GrievanceType::SettledNearBorders, PlayerId{1}); // -10
    aoc::sim::applyGrievanceModifiers(w.gameState, d);
    const aoc::sim::RelationModifier* m = grievanceModifier(d, PlayerId{0}, PlayerId{1});
    REQUIRE(m != nullptr);
    CHECK(m->amount == -15);
    CHECK(grievanceModifier(d, PlayerId{1}, PlayerId{0}) == nullptr);

    // Re-applying keeps a single slot; the cap holds for large totals. Repeats of
    // one type collapse into the existing grievance, so use distinct types
    // (-30 and -30 on top of -30: |total| 90, half 45, capped at 40).
    holder.grievances().addGrievance(aoc::sim::GrievanceType::DeclaredWarOnAlly, PlayerId{1});
    holder.grievances().addGrievance(aoc::sim::GrievanceType::BrokeNonAggression, PlayerId{1});
    aoc::sim::applyGrievanceModifiers(w.gameState, d);
    int32_t slots = 0;
    for (const aoc::sim::RelationModifier& mod : d.relation(PlayerId{0}, PlayerId{1}).modifiers) {
        if (mod.reason == "Grievances") {
            ++slots;
            CHECK(mod.amount == -40);
        }
    }
    CHECK(slots == 1);
}

TEST_CASE("declaring war fans out obligations to the target's allies through the installed tracker") {
    aoc::test::World w = aoc::test::makeWorld(3);
    DiplomacyManager d;
    d.initialize(3);
    aoc::sim::AllianceObligationTracker tracker;
    d.setAllianceTracker(&tracker);
    d.relation(PlayerId{1}, PlayerId{2}).hasMilitaryAlliance = true;
    d.relation(PlayerId{2}, PlayerId{1}).hasMilitaryAlliance = true;

    d.declareWar(PlayerId{0}, PlayerId{1});
    REQUIRE(tracker.pendingObligations.size() == 1);
    CHECK(tracker.pendingObligations[0].obligatedPlayer == PlayerId{2});
    CHECK(tracker.pendingObligations[0].attacker == PlayerId{0});
    CHECK(tracker.pendingObligations[0].defender == PlayerId{1});
    CHECK(tracker.pendingObligations[0].turnsToRespond == 5);
}
