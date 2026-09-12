/**
 * @file test_blockade.cpp
 * @brief Blockade (plan B5, 3.4): an enemy warship on the water beside a
 *        coastal city closes its port, and what that costs. Detection is
 *        driven through war and updateBlockades; the effects are driven from
 *        the state directly, so a change to either side fails on its own.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/simulation/city/CitySiege.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/city/Happiness.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/economy/Maintenance.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/economy/TradeRouteSystem.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"

using aoc::PlayerId;
using aoc::hex::AxialCoord;
using aoc::map::TerrainType;

namespace {

constexpr PlayerId P0{0};
constexpr PlayerId P1{1};
constexpr aoc::UnitTypeId GALLEY{6};  ///< Naval, Ancient.
constexpr aoc::UnitTypeId WARRIOR{0}; ///< Melee: a soldier cannot close a port.
constexpr aoc::UnitTypeId TRADER{30}; ///< Nor can a merchantman.

/// A port city of P0 at (5,5) with open water at (6,5), and P1's fleet in
/// reach. Nobody is at war until a case says so.
struct Port {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::sim::DiplomacyManager d;
    aoc::game::City* city = nullptr;
    const AxialCoord water{6, 5};

    Port() {
        this->d.initialize(2);
        this->d.meetPlayers(P0, P1, 1);
        this->w.grid.setTerrain(this->w.grid.toIndex(this->water), TerrainType::Coast);
        this->city = &aoc::test::addCityAt(this->w, P0, 5, 5, "Piraeus");
        aoc::test::addCityAt(this->w, P1, 18, 9, "Carthage");
    }

    [[nodiscard]] PlayerId blockader() const {
        return aoc::sim::blockaderOf(this->w.gameState, this->w.grid, &this->d, *this->city);
    }
    void tick() { aoc::sim::updateBlockades(this->w.gameState, this->w.grid, &this->d); }
};

} // namespace

TEST_CASE("a fleet at war beside the port closes it, and peace opens it again") {
    Port p;
    aoc::test::addUnitAt(p.w, P1, GALLEY, p.water.q, p.water.r);
    CHECK(p.blockader() == aoc::INVALID_PLAYER); // at peace it is just a ship

    p.d.declareWar(P0, P1);
    CHECK(p.blockader() == P1);
    p.tick();
    CHECK(p.city->combat().blockadedBy == P1);
    CHECK(p.city->combat().blockadedTurns == 1);
    p.tick();
    CHECK(p.city->combat().blockadedTurns == 2); // unbroken, so it accumulates

    p.d.makePeace(P0, P1);
    p.tick();
    CHECK(p.city->combat().blockadedBy == aoc::INVALID_PLAYER);
    CHECK(p.city->combat().blockadedTurns == 0);
}

TEST_CASE("only an enemy warship on the water, and only within reach, closes a port") {
    SUBCASE("a soldier on the shore does not") {
        Port p;
        p.d.declareWar(P0, P1);
        aoc::test::addUnitAt(p.w, P1, WARRIOR, 5, 6);
        CHECK(p.blockader() == aoc::INVALID_PLAYER);
    }
    SUBCASE("a merchantman does not") {
        Port p;
        p.d.declareWar(P0, P1);
        aoc::test::addUnitAt(p.w, P1, TRADER, p.water.q, p.water.r);
        CHECK(p.blockader() == aoc::INVALID_PLAYER);
    }
    SUBCASE("a warship one tile further out does not") {
        Port p;
        p.d.declareWar(P0, P1);
        p.w.grid.setTerrain(p.w.grid.toIndex(AxialCoord{7, 5}), TerrainType::Coast);
        aoc::test::addUnitAt(p.w, P1, GALLEY, 7, 5);
        CHECK(p.blockader() == aoc::INVALID_PLAYER);
    }
    SUBCASE("a warship sharing its tile with a civilian still does") {
        // The lookup that answers "what stands here" returns one unit per
        // civ per tile, so a fleet is scanned instead of the tiles.
        Port p;
        p.d.declareWar(P0, P1);
        aoc::test::addUnitAt(p.w, P1, TRADER, p.water.q, p.water.r);
        aoc::test::addUnitAt(p.w, P1, GALLEY, p.water.q, p.water.r);
        CHECK(p.blockader() == P1);
    }
    SUBCASE("our own fleet never does") {
        Port p;
        p.d.declareWar(P0, P1);
        aoc::test::addUnitAt(p.w, P0, GALLEY, p.water.q, p.water.r);
        CHECK(p.blockader() == aoc::INVALID_PLAYER);
    }
    SUBCASE("an inland city has no port to close") {
        Port p;
        p.d.declareWar(P0, P1);
        aoc::game::City& inland = aoc::test::addCityAt(p.w, P0, 12, 12, "Sparta");
        aoc::test::addUnitAt(p.w, P1, GALLEY, p.water.q, p.water.r);
        CHECK(aoc::sim::blockaderOf(p.w.gameState, p.w.grid, &p.d, inland) == aoc::INVALID_PLAYER);
        p.tick();
        CHECK(inland.combat().blockadedBy == aoc::INVALID_PLAYER);
        CHECK(p.city->combat().blockadedBy == P1); // the port beside it is closed
    }
}

TEST_CASE("a Harbor tile is a station too, wherever the district sits") {
    Port p;
    p.d.declareWar(P0, P1);
    // Two tiles out, so the neighbour rule cannot find it: only the Harbor
    // branch can, and deleting that branch must fail this case.
    const AxialCoord harbourTile{3, 6};
    REQUIRE(p.w.grid.distance(p.city->location(), harbourTile) == 2);
    p.w.grid.setTerrain(p.w.grid.toIndex(harbourTile), TerrainType::Coast);
    p.city->districts().districts.push_back({aoc::sim::DistrictType::Harbor, harbourTile, {}});
    aoc::test::addUnitAt(p.w, P1, GALLEY, harbourTile.q, harbourTile.r);
    CHECK(p.blockader() == P1);
}

TEST_CASE("a blockaded port stops collecting from the water it works") {
    // The other half of the suppression: worked sea tiles, no Harbor district
    // in sight, so only the water-tile rule can move the number.
    Port p;
    p.w.gameState.player(P0)->monetary().system = aoc::sim::MonetarySystemType::CommodityMoney;
    p.city->setPopulation(8);
    p.city->workedTiles().push_back(p.water); // Coast: one gold a turn
    const float open = aoc::sim::collectionEfficiency(*p.w.gameState.player(P0), p.w.grid);
    p.city->combat().blockadedBy = P1;
    CHECK(aoc::sim::collectionEfficiency(*p.w.gameState.player(P0), p.w.grid) < open);
}

TEST_CASE("a blockaded Harbor collects nothing while the fleet sits there") {
    Port p;
    p.w.gameState.player(P0)->monetary().system = aoc::sim::MonetarySystemType::CommodityMoney;
    p.city->setPopulation(8);
    p.city->districts().districts.push_back({aoc::sim::DistrictType::Harbor, {4, 6}, {}});
    const float open = aoc::sim::collectionEfficiency(*p.w.gameState.player(P0), p.w.grid);

    p.city->combat().blockadedBy = P1;
    const float closed = aoc::sim::collectionEfficiency(*p.w.gameState.player(P0), p.w.grid);
    CHECK(closed < open);
}

TEST_CASE("the people bear a blockade for five turns, and then they do not") {
    Port p;
    aoc::game::Player& owner = *p.w.gameState.player(P0);
    p.city->setPopulation(6);
    p.city->combat().blockadedBy    = P1;
    p.city->combat().blockadedTurns = aoc::sim::BLOCKADE_AMENITY_TURNS - 1;
    aoc::sim::computeCityHappiness(owner, nullptr);
    const float borne = p.city->happiness().amenities;

    p.city->combat().blockadedTurns = aoc::sim::BLOCKADE_AMENITY_TURNS;
    aoc::sim::computeCityHappiness(owner, nullptr);
    CHECK(p.city->happiness().amenities ==
          doctest::Approx(borne - aoc::sim::BLOCKADE_AMENITY_PENALTY));
}

TEST_CASE("a blockade signs no new sea route, and the lane it closes is only the sea one") {
    aoc::test::World w = aoc::test::makeWorld(1, 30, 16);
    for (int32_t q = 6; q <= 10; ++q) {
        w.grid.setTerrain(w.grid.toIndex(AxialCoord{q, 5}), TerrainType::Coast);
    }
    aoc::game::City& home  = aoc::test::addCityAt(w, P0, 5, 5, "Piraeus");
    aoc::game::City& other = aoc::test::addCityAt(w, P0, 11, 5, "Delos");
    home.stockpile().addGoods(aoc::sim::goods::COAL, 200); // a voyage burns coal
    aoc::game::Unit& trader = aoc::test::addUnitAt(w, P0, TRADER, 5, 5);
    aoc::sim::Market market;
    market.initialize();
    REQUIRE(w.grid.isCoastal(home.location()));
    REQUIRE(w.grid.isCoastal(other.location()));

    other.combat().blockadedBy = P1;
    CHECK(aoc::sim::establishTradeRoute(w.gameState, w.grid, market, nullptr, trader, other) ==
          aoc::ErrorCode::TradeRouteBlockaded);

    // The same pair overland is nobody's business: an inland city is reachable.
    aoc::game::City& inland = aoc::test::addCityAt(w, P0, 5, 9, "Sparta");
    inland.combat().blockadedBy = P1; // a blockade it cannot have, and would not stop a road
    CHECK(aoc::sim::establishTradeRoute(w.gameState, w.grid, market, nullptr, trader, inland) ==
          aoc::ErrorCode::Ok);
}

TEST_CASE("a sea route already at sea waits at anchor, and gives up if the fleet stays") {
    aoc::test::World w = aoc::test::makeWorld(1, 30, 16);
    for (int32_t q = 6; q <= 10; ++q) {
        w.grid.setTerrain(w.grid.toIndex(AxialCoord{q, 5}), TerrainType::Coast);
    }
    aoc::game::City& home   = aoc::test::addCityAt(w, P0, 5, 5, "Piraeus");
    aoc::game::City& other  = aoc::test::addCityAt(w, P0, 11, 5, "Delos");
    home.stockpile().addGoods(aoc::sim::goods::COAL, 200);
    other.stockpile().addGoods(aoc::sim::goods::COAL, 200);
    aoc::game::Unit& trader = aoc::test::addUnitAt(w, P0, TRADER, 5, 5);
    aoc::sim::Market market;
    market.initialize();
    REQUIRE(aoc::sim::establishTradeRoute(w.gameState, w.grid, market, nullptr, trader, other) ==
            aoc::ErrorCode::Ok);
    REQUIRE(trader.trader().routeType == aoc::sim::TradeRouteType::Sea);

    other.combat().blockadedBy    = P1;
    other.combat().blockadedTurns = 3;
    const int32_t before = trader.trader().pathIndex;
    aoc::sim::processTradeRoutes(w.gameState, w.grid, market, nullptr);
    CHECK(trader.trader().pathIndex == before); // at anchor

    other.combat().blockadedTurns = aoc::sim::BLOCKADE_ABANDON_TURNS;
    const std::size_t fleet       = w.gameState.player(P0)->units().size();
    aoc::sim::processTradeRoutes(w.gameState, w.grid, market, nullptr);
    CHECK(w.gameState.player(P0)->units().size() == fleet - 1); // the contract is off
}
