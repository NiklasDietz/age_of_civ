/**
 * @file test_government_plaza_and_dam.cpp
 * @brief Phase 3 item 5: the Government Plaza (47) grants one wildcard policy slot
 *        through a count derived each turn from the player's buildings, and the Dam
 *        improvement protects a floodplain from flood damage (until 2026-09-05 only
 *        a Fort did, as a stand-in).
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/RiverGameplay.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/government/Government.hpp"
#include "aoc/simulation/government/GovernmentComponent.hpp"

#include <algorithm>

using aoc::BuildingId;
using aoc::CivicId;
using aoc::PlayerId;
using aoc::sim::buildingDef;
using aoc::sim::buildingWildcardSlots;
using aoc::sim::DistrictType;
using aoc::sim::MAX_POLICY_SLOTS;
using aoc::sim::wildcardSlotCount;

namespace {

constexpr BuildingId GOVERNMENT_PLAZA{47};

} // namespace

TEST_CASE("the Government Plaza has a row, the State Workforce gate and one wildcard slot") {
    CHECK(aoc::sim::BUILDING_DEFS.size() >= 48);
    const aoc::sim::BuildingDef& def = buildingDef(GOVERNMENT_PLAZA);
    CHECK(def.name == "Government Plaza");
    CHECK(def.requiredDistrict == DistrictType::CityCenter);
    CHECK(def.requiredCivic == CivicId{14}); // State Workforce
    CHECK(buildingWildcardSlots(GOVERNMENT_PLAZA) == 1);
    CHECK(buildingWildcardSlots(BuildingId{0}) == 0);
    CHECK(buildingWildcardSlots(BuildingId{46}) == 0);
    for (std::size_t i = 0; i < aoc::sim::BUILDING_DEFS.size(); ++i) {
        CHECK(aoc::sim::BUILDING_DEFS[i].id.value == i);
    }
}

TEST_CASE(
    "processGovernment derives the bonus slot from the building; the count caps at the maximum") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha");
    aoc::game::Player& player                = *w.gameState.player(PlayerId{0});
    aoc::game::City& city                    = *player.cities()[0];
    aoc::sim::PlayerGovernmentComponent& gov = player.government();
    const aoc::sim::GovernmentDef& gdef      = aoc::sim::governmentDef(gov.government);
    const uint8_t fixedSlots =
        static_cast<uint8_t>(gdef.militarySlots + gdef.economicSlots + gdef.diplomaticSlots);

    aoc::sim::processGovernment(player);
    CHECK(gov.bonusWildcardSlots == 0);
    CHECK(wildcardSlotCount(gov) == gdef.wildcardSlots);

    city.districts().districts[0].buildings.push_back(GOVERNMENT_PLAZA);
    aoc::sim::processGovernment(player);
    CHECK(gov.bonusWildcardSlots == 1);
    const uint8_t expected = static_cast<uint8_t>(
        std::min<int32_t>(gdef.wildcardSlots + 1, MAX_POLICY_SLOTS - fixedSlots));
    CHECK(wildcardSlotCount(gov) == expected);
    CHECK(fixedSlots + wildcardSlotCount(gov) <= MAX_POLICY_SLOTS);

    // The rival has no Plaza and keeps the government's own count.
    aoc::game::Player& rival = *w.gameState.player(PlayerId{1});
    aoc::sim::processGovernment(rival);
    CHECK(rival.government().bonusWildcardSlots == 0);

    // Losing the building takes the slot back next turn.
    city.districts().districts[0].buildings.clear();
    aoc::sim::processGovernment(player);
    CHECK(gov.bonusWildcardSlots == 0);
}

TEST_CASE("a Dam keeps a floodplain's improvement through flood seasons; an unprotected Farm is "
          "washed away") {
    aoc::test::World w = aoc::test::makeWorld(2);
    const int32_t tile = w.grid.toIndex(aoc::hex::AxialCoord{5, 5});
    w.grid.setFeature(tile, aoc::map::FeatureType::Floodplains);
    w.grid.setRiverEdges(tile, 1);

    // Unprotected: the deterministic per-turn hash destroys the Farm eventually
    // (about 1.5% per flood season; 1000 seasons make a miss astronomically unlikely).
    w.grid.setImprovement(tile, aoc::map::ImprovementType::Farm);
    int32_t destroyedAt = -1;
    for (int32_t turn = 4; turn <= 4000 && destroyedAt < 0; turn += 4) {
        aoc::map::processFlooding(w.gameState, w.grid, turn);
        if (w.grid.improvement(tile) == aoc::map::ImprovementType::None) {
            destroyedAt = turn;
        }
    }
    CHECK(destroyedAt > 0);

    // A Dam on the tile: never touched over the same seasons.
    w.grid.setImprovement(tile, aoc::map::ImprovementType::Dam);
    for (int32_t turn = 4; turn <= 4000; turn += 4) {
        aoc::map::processFlooding(w.gameState, w.grid, turn);
    }
    CHECK(w.grid.improvement(tile) == aoc::map::ImprovementType::Dam);

    // The Fort stand-in still protects (kept so existing forts do not regress).
    w.grid.setImprovement(tile, aoc::map::ImprovementType::Fort);
    for (int32_t turn = 4; turn <= 4000; turn += 4) {
        aoc::map::processFlooding(w.gameState, w.grid, turn);
    }
    CHECK(w.grid.improvement(tile) == aoc::map::ImprovementType::Fort);
}
