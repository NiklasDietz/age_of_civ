/**
 * @file test_district_adjacency.cpp
 * @brief One adjacency path: every yield a district earns from its neighbours
 *        reaches the city through cityAdjacencyYields, and only the owner's own
 *        districts count.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/simulation/city/CityScience.hpp"
#include "aoc/simulation/city/DistrictAdjacency.hpp"
#include "aoc/simulation/city/DistrictPlacement.hpp"
#include "aoc/simulation/economy/Maintenance.hpp"
#include "aoc/simulation/religion/Religion.hpp"

#include <vector>

using aoc::PlayerId;
using aoc::hex::AxialCoord;
using aoc::sim::AdjacencyBonus;
using aoc::sim::DistrictIndex;
using aoc::sim::DistrictType;

namespace {

/// One city on grassland, its whole work radius owned, so a district can stand
/// anywhere the rules allow.
struct Fixture {
    aoc::test::World world = aoc::test::makeWorld(2);
    aoc::game::City* city  = nullptr;

    Fixture() {
        this->city = &aoc::test::addCityAt(this->world, PlayerId{0}, 8, 6, "Alpha");
        this->city->setPopulation(4);
        std::vector<AxialCoord> nearby;
        aoc::hex::spiral(this->city->location(), aoc::sim::CITY_WORK_RADIUS,
                         std::back_inserter(nearby));
        for (const AxialCoord& tile : nearby) {
            if (this->world.grid.isValid(tile)) {
                this->world.grid.setOwner(this->world.grid.toIndex(tile), PlayerId{0});
            }
        }
    }

    aoc::sim::CityDistrictsComponent::PlacedDistrict& place(DistrictType type, AxialCoord at) {
        this->city->districts().districts.push_back({type, at, {}});
        return this->city->districts().districts.back();
    }

    [[nodiscard]] AdjacencyBonus yields() const {
        DistrictIndex index;
        index.build(*this->world.gameState.player(PlayerId{0}));
        return aoc::sim::cityAdjacencyYields(this->world.grid, index, *this->city);
    }
};

} // namespace

TEST_CASE("a Theatre Square earns culture from wonders and from neighbouring districts") {
    Fixture f;
    const AxialCoord centre  = f.city->location();
    const AxialCoord theatre = {centre.q + 2, centre.r};
    f.place(DistrictType::Theatre, theatre);

    const AdjacencyBonus bare = f.yields();
    CHECK(bare.culture == doctest::Approx(0.0f)); // nothing adjacent yet

    f.world.grid.setNaturalWonder(f.world.grid.toIndex(AxialCoord{theatre.q + 1, theatre.r}),
                                  aoc::map::NaturalWonderType::GrandCanyon);
    const AdjacencyBonus withWonder = f.yields();
    CHECK(withWonder.culture == doctest::Approx(bare.culture + 2.0f));

    // A second Theatre Square beside the first: both sides see the cluster.
    f.place(DistrictType::Theatre, {theatre.q, theatre.r + 1});
    const AdjacencyBonus clustered = f.yields();
    CHECK(clustered.culture > withWonder.culture + 1.0f);
}

TEST_CASE("adjacent Campus districts cluster their science") {
    Fixture f;
    const AxialCoord centre = f.city->location();
    f.place(DistrictType::Campus, {centre.q + 2, centre.r});
    const float single = f.yields().science;

    f.place(DistrictType::Campus, {centre.q + 2, centre.r + 1});
    const float clustered = f.yields().science;
    // Each of the two now sees one adjacent Campus, so the pair is worth +2.
    CHECK(clustered == doctest::Approx(single + 2.0f));
}

TEST_CASE("a foreign district next door grants nothing") {
    Fixture f;
    const AxialCoord centre = f.city->location();
    const AxialCoord mine   = {centre.q + 2, centre.r};
    f.place(DistrictType::Commercial, mine);
    const float alone = f.yields().gold;

    aoc::game::City& theirs =
        aoc::test::addCityAt(f.world, PlayerId{1}, mine.q + 1, mine.r, "Beta");
    theirs.districts().districts.push_back({DistrictType::Harbor, {mine.q + 1, mine.r}, {}});
    CHECK(f.yields().gold == doctest::Approx(alone));
}

TEST_CASE("Holy Site faith arrives once, through the shared path") {
    Fixture f;
    const AxialCoord centre = f.city->location();
    const AxialCoord site   = {centre.q + 2, centre.r};
    f.place(DistrictType::HolySite, site);
    f.world.grid.setTerrain(f.world.grid.toIndex(AxialCoord{site.q + 1, site.r}),
                            aoc::map::TerrainType::Mountain);

    const float adjacencyFaith = f.yields().faith;
    CHECK(adjacencyFaith == doctest::Approx(1.0f)); // one adjacent mountain

    aoc::game::Player& player = *f.world.gameState.player(PlayerId{0});
    const float before        = player.faith().faith;
    aoc::sim::accumulateFaith(player, f.world.grid);
    const float gained = player.faith().faith - before;
    // 1 per city + 2 for the Holy Site + the adjacency, counted exactly once.
    CHECK(gained == doctest::Approx(3.0f + adjacencyFaith));
}

TEST_CASE("Commercial river gold is collection reach, and the treasury sees it") {
    Fixture f;
    const AxialCoord centre = f.city->location();
    const AxialCoord hub    = {centre.q + 2, centre.r};
    f.place(DistrictType::Commercial, hub);

    aoc::game::Player& player = *f.world.gameState.player(PlayerId{0});
    player.monetary().system =
        aoc::sim::MonetarySystemType::CommodityMoney; // past the barter guard
    player.monetary().privateSpecie = 100000;         // something to tax

    const float dryReach                  = aoc::sim::collectionEfficiency(player, f.world.grid);
    const aoc::CurrencyAmount dryTreasury = aoc::sim::processGoldIncome(player, f.world.grid);

    player.monetary().privateSpecie = 100000;
    f.world.grid.setRiverEdges(f.world.grid.toIndex(hub), 0x01);
    const float wetReach                  = aoc::sim::collectionEfficiency(player, f.world.grid);
    const aoc::CurrencyAmount wetTreasury = aoc::sim::processGoldIncome(player, f.world.grid);

    // The river is worth +2 gold points to the Commercial Hub, now two points
    // of reach (0.01 each, scaled by the city's weight and multipliers), and
    // the tax the treasury takes rises with it.
    CHECK(wetReach > dryReach);
    CHECK(wetTreasury > dryTreasury);
}

TEST_CASE("district adjacency reaches player science and culture") {
    Fixture f;
    const AxialCoord centre   = f.city->location();
    const AxialCoord campus   = {centre.q + 2, centre.r};
    aoc::game::Player& player = *f.world.gameState.player(PlayerId{0});

    f.place(DistrictType::Campus, campus);
    const float scienceOne = aoc::sim::computePlayerScience(player, f.world.grid);
    CHECK(f.yields().science == doctest::Approx(0.0f));

    // A second Campus beside the first changes nothing but the adjacency, so
    // any rise in player science came through the shared path. The accumulator
    // scales it by the city and player multipliers, so the exact figure is a
    // balance number; that it arrives at all is the contract.
    f.place(DistrictType::Campus, {campus.q, campus.r + 1});
    CHECK(f.yields().science == doctest::Approx(2.0f));
    const float scienceTwo = aoc::sim::computePlayerScience(player, f.world.grid);
    CHECK(scienceTwo > scienceOne + 1.0f);

    const float cultureOne = aoc::sim::computePlayerCulture(player, f.world.grid);
    f.place(DistrictType::Theatre, {centre.q - 2, centre.r});
    f.place(DistrictType::Theatre, {centre.q - 2, centre.r + 1});
    const float cultureTwo = aoc::sim::computePlayerCulture(player, f.world.grid);
    CHECK(cultureTwo > cultureOne);
}
