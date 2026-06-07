/**
 * @file test_zoc_movement.cpp
 * @brief Diplomacy-aware Zone-of-Control freezing in unit movement.
 *
 * Verifies that moveUnitAlongPath consults diplomacy when deciding whether an
 * enemy military unit's ZoC should freeze the mover:
 *   (a) enemy military adjacent + at war          -> movement frozen
 *   (b) enemy military adjacent + open borders     -> NOT frozen (the fix)
 *   (c) diplomacy == nullptr + enemy adjacent      -> frozen (adjacency-only)
 */

#include "aoc/simulation/unit/Movement.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"

#include <cassert>
#include <cstdio>

using aoc::hex::AxialCoord;
using aoc::map::HexGrid;
using aoc::map::TerrainType;

namespace {

// Warrior (UnitTypeId 0): Melee / military, 2 movement points.
constexpr aoc::UnitTypeId WARRIOR{0};

// Scenario geometry (axial). The mover starts at M and is ordered along the
// straight path M -> T -> T2. An enemy military unit sits at E, which is a
// neighbour of T (so it exerts ZoC on T). When ZoC freezes the mover it stops
// at T; when it does not, the mover continues to T2.
constexpr AxialCoord M {3, 3};
constexpr AxialCoord T {4, 3};
constexpr AxialCoord T2{5, 3};
constexpr AxialCoord E {4, 4};

/// Build a small all-land grid so every tile on the path is passable (cost 1).
HexGrid makeLandGrid() {
    HexGrid grid;
    grid.initialize(10, 10);
    for (int32_t i = 0; i < grid.tileCount(); ++i) {
        grid.setTerrain(i, TerrainType::Grassland);
    }
    return grid;
}

/// Place the mover (player 0) at M with a 2-tile pending path, and an enemy
/// military unit (player 1) at E. Returns a pointer to the mover.
aoc::game::Unit* setupScenario(aoc::game::GameState& gameState) {
    gameState.initialize(2);

    aoc::game::Unit& mover = gameState.player(0)->addUnit(WARRIOR, M);
    mover.setMovementRemaining(2);
    mover.pendingPath().assign({T, T2});

    (void)gameState.player(1)->addUnit(WARRIOR, E);
    return &mover;
}

void test_atWar_freezesMovement() {
    HexGrid grid = makeLandGrid();
    aoc::game::GameState gameState;
    aoc::game::Unit* mover = setupScenario(gameState);

    aoc::sim::DiplomacyManager diplomacy;
    diplomacy.initialize(2);
    diplomacy.meetPlayers(0, 1, 0);
    diplomacy.declareWar(0, 1);

    aoc::sim::moveUnitAlongPath(gameState, *mover, grid, &diplomacy);

    // Entered T and stopped: enemy ZoC consumed the rest of the movement.
    assert(mover->position() == T);
    assert(mover->movementRemaining() == 0);
}

void test_openBorders_doesNotFreeze() {
    HexGrid grid = makeLandGrid();
    aoc::game::GameState gameState;
    aoc::game::Unit* mover = setupScenario(gameState);

    aoc::sim::DiplomacyManager diplomacy;
    diplomacy.initialize(2);
    diplomacy.meetPlayers(0, 1, 0);
    diplomacy.grantOpenBorders(0, 1);   // open borders, not at war

    aoc::sim::moveUnitAlongPath(gameState, *mover, grid, &diplomacy);

    // Open borders exempts the neighbour's ZoC: the mover walks straight to T2.
    assert(mover->position() == T2);
}

void test_nullDiplomacy_adjacencyOnlyFreezes() {
    HexGrid grid = makeLandGrid();
    aoc::game::GameState gameState;
    aoc::game::Unit* mover = setupScenario(gameState);

    // No diplomacy context -> adjacency-only fallback: any adjacent enemy
    // military unit freezes the mover, preserving the prior behaviour.
    aoc::sim::moveUnitAlongPath(gameState, *mover, grid, nullptr);

    assert(mover->position() == T);
    assert(mover->movementRemaining() == 0);
}

} // namespace

int main() {
    test_atWar_freezesMovement();
    test_openBorders_doesNotFreeze();
    test_nullDiplomacy_adjacencyOnlyFreezes();
    std::printf("test_zoc_movement: OK\n");
    return 0;
}
