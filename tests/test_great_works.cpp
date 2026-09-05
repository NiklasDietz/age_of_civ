/**
 * @file test_great_works.cpp
 * @brief Great Works: slots are capacity, placed works are what counts. A Great
 *        Artist's activation places a work of Art in the nearest own city with a free
 *        slot and only culture-bombs when none exists; tourism counts works, not slots;
 *        a captured city leaves an antiquity site in the sparse grid layer.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/culture/GreatWorks.hpp"
#include "aoc/simulation/culture/Tourism.hpp"
#include "aoc/simulation/greatpeople/GreatPeople.hpp"

using aoc::BuildingId;
using aoc::PlayerId;
using aoc::sim::DistrictType;
using aoc::sim::GreatWork;
using aoc::sim::GreatWorkType;

namespace {

constexpr BuildingId AMPHITHEATER{39}; // 2 slots
constexpr BuildingId ART_MUSEUM{40};   // 3 slots
constexpr aoc::UnitTypeId GREAT_PERSON{102};

/// Give `city` a Theatre Square holding `building`.
void addTheatre(aoc::game::City& city, BuildingId building) {
    city.districts().districts.push_back(
        {DistrictType::Theatre, {city.location().q + 1, city.location().r}, {building}});
}

/// Recruit one Artist for player 0; it spawns on the capital tile.
aoc::game::Unit* recruitArtist(aoc::test::World& w) {
    aoc::game::Player& p = *w.gameState.players()[0];
    p.greatPeople().points[static_cast<uint8_t>(aoc::sim::GreatPersonType::Artist)] =
        p.greatPeople().threshold(aoc::sim::GreatPersonType::Artist) + 1.0f;
    aoc::sim::checkGreatPeopleRecruitment(w.gameState, PlayerId{0});
    for (const std::unique_ptr<aoc::game::Unit>& u : p.units()) {
        if (u->typeId() == GREAT_PERSON) {
            return u.get();
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("capacity comes from building slots and placement respects it") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha");
    aoc::game::City& city = *w.gameState.player(PlayerId{0})->cities()[0];
    CHECK(aoc::sim::greatWorkCapacity(city) == 0);
    CHECK_FALSE(aoc::sim::placeGreatWork(city, {GreatWorkType::Art, PlayerId{0}, 3, 10}));

    addTheatre(city, AMPHITHEATER);
    CHECK(aoc::sim::greatWorkCapacity(city) == 2);
    CHECK(aoc::sim::freeGreatWorkSlots(city) == 2);
    CHECK(aoc::sim::placeGreatWork(city, {GreatWorkType::Writing, PlayerId{0}, 3, 10}));
    CHECK(aoc::sim::placeGreatWork(city, {GreatWorkType::Music, PlayerId{0}, 4, 11}));
    CHECK_FALSE(aoc::sim::placeGreatWork(city, {GreatWorkType::Art, PlayerId{0}, 5, 12})); // full
    CHECK(city.greatWorks().works.size() == 2);
    CHECK(aoc::sim::freeGreatWorkSlots(city) == 0);

    const aoc::sim::GreatWorkTally tally =
        aoc::sim::tallyGreatWorks(*w.gameState.player(PlayerId{0}));
    CHECK(tally.works == 2);
    CHECK(tally.capacity == 2);
    CHECK(aoc::sim::greatWorkTypeName(GreatWorkType::Artifact) == "Artifact");
}

TEST_CASE("the nearest own city with a free slot is chosen") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 3, 3, "Near");
    aoc::test::addCityAt(w, PlayerId{0}, 15, 9, "Far");
    aoc::game::Player& p      = *w.gameState.player(PlayerId{0});
    aoc::game::City& nearCity = *p.cities()[0];
    aoc::game::City& farCity  = *p.cities()[1];
    CHECK(aoc::sim::cityWithFreeGreatWorkSlot(p, w.grid, {4, 3}) == nullptr);

    addTheatre(farCity, ART_MUSEUM);
    CHECK(aoc::sim::cityWithFreeGreatWorkSlot(p, w.grid, {4, 3}) == &farCity); // the only one
    addTheatre(nearCity, AMPHITHEATER);
    CHECK(aoc::sim::cityWithFreeGreatWorkSlot(p, w.grid, {4, 3}) == &nearCity);
    CHECK(aoc::sim::placeGreatWork(nearCity, {GreatWorkType::Art, PlayerId{0}, 1, 1}));
    CHECK(aoc::sim::placeGreatWork(nearCity, {GreatWorkType::Art, PlayerId{0}, 1, 1}));
    CHECK(aoc::sim::cityWithFreeGreatWorkSlot(p, w.grid, {4, 3}) == &farCity); // near is full
}

TEST_CASE("an activated Artist places a work of Art when a slot exists, otherwise culture-bombs") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha");
    aoc::game::City& city = *w.gameState.player(PlayerId{0})->cities()[0];
    addTheatre(city, AMPHITHEATER);

    aoc::game::Unit* artist = recruitArtist(w);
    REQUIRE(artist != nullptr);
    REQUIRE(aoc::sim::requestGreatPersonActivation(w.gameState, w.grid, PlayerId{0},
                                                   artist->position()) == aoc::ErrorCode::Ok);
    REQUIRE(city.greatWorks().works.size() == 1);
    CHECK(city.greatWorks().works[0].type == GreatWorkType::Art);
    CHECK(city.greatWorks().works[0].creator == PlayerId{0});
    CHECK(city.greatWorks().works[0].namedId != 0xFF);

    // Without a free slot the old culture bomb still happens: unowned tiles near the person.
    aoc::test::World w2 = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w2, PlayerId{0}, 5, 5, "Beta");
    aoc::game::Unit* artist2 = recruitArtist(w2);
    REQUIRE(artist2 != nullptr);
    const int32_t farTile =
        w2.grid.toIndex(aoc::hex::AxialCoord{artist2->position().q + 2, artist2->position().r});
    w2.grid.setOwner(farTile, aoc::INVALID_PLAYER);
    REQUIRE(aoc::sim::requestGreatPersonActivation(w2.gameState, w2.grid, PlayerId{0},
                                                   artist2->position()) == aoc::ErrorCode::Ok);
    CHECK(w2.grid.owner(farTile) == PlayerId{0});
    CHECK(w2.gameState.player(PlayerId{0})->cities()[0]->greatWorks().works.empty());
}

TEST_CASE("tourism counts placed works, not empty slots") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha");
    aoc::game::Player& p  = *w.gameState.player(PlayerId{0});
    aoc::game::City& city = *p.cities()[0];
    addTheatre(city, ART_MUSEUM); // three empty slots

    aoc::sim::computeTourism(w.gameState, PlayerId{0}, w.grid, nullptr);
    CHECK(p.tourism().greatWorkCount == 0);
    const float emptyTourism = p.tourism().tourismPerTurn;

    REQUIRE(aoc::sim::placeGreatWork(city, {GreatWorkType::Art, PlayerId{0}, 2, 30}));
    aoc::sim::computeTourism(w.gameState, PlayerId{0}, w.grid, nullptr);
    CHECK(p.tourism().greatWorkCount == 1);
    CHECK(p.tourism().tourismPerTurn > emptyTourism);
}

TEST_CASE("the antiquity layer is sparse, cleared by fallout and by initialize") {
    aoc::test::World w = aoc::test::makeWorld(2);
    const int32_t tile = w.grid.toIndex(aoc::hex::AxialCoord{7, 7});
    CHECK(w.grid.antiquitySite(tile) == 0);
    w.grid.setAntiquitySite(tile, 1);
    CHECK(w.grid.antiquitySite(tile) == 1);
    w.grid.setAntiquitySite(tile, 0);
    CHECK(w.grid.antiquitySite(tile) == 0);

    w.grid.setAntiquitySite(tile, 2);
    w.grid.applyFallout(tile, 3);
    CHECK(w.grid.antiquitySite(tile) == 0); // fallout wipes the site like every other tile mark

    w.grid.setAntiquitySite(tile, 1);
    w.grid.initialize(24, 16);
    CHECK(w.grid.antiquitySite(tile) == 0);
}
