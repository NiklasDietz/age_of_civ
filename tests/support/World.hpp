#pragma once

/**
 * @file World.hpp
 * @brief Shared test fixture. Every sim/UI test used to hand-roll the same
 *        "grassland grid + N players" setup; this is that setup, once.
 */

#include "aoc/game/City.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/HexGrid.hpp"

#include <cstdint>
#include <string>

namespace aoc::test {

/// A game state plus a matching all-grassland grid. Move-only, like GameState.
struct World {
    aoc::game::GameState gameState;
    aoc::map::HexGrid grid;
};

/// `playerCount` players on a `width` x `height` grassland map.
[[nodiscard]] inline World makeWorld(int32_t playerCount = 2, int32_t width = 24,
                                     int32_t height = 16) {
    World world;
    world.grid.initialize(width, height);
    for (int32_t i = 0; i < world.grid.tileCount(); ++i) {
        world.grid.setTerrain(i, aoc::map::TerrainType::Grassland);
    }
    world.gameState.initialize(playerCount);
    return world;
}

/// Found `name` for `owner` at (q, r). The first city of a player is marked an
/// original capital, without which the conquest-elimination rule fires.
inline aoc::game::City& addCityAt(World& world, aoc::PlayerId owner, int32_t q, int32_t r,
                                  const std::string& name) {
    aoc::game::Player* player = world.gameState.player(owner);
    const bool isFirst        = player->cities().empty();
    aoc::game::City& city     = player->addCity({q, r}, name);
    if (isFirst) {
        city.setOriginalCapital(true);
    }
    return city;
}

/// Place a unit of `typeId` for `owner` at (q, r).
inline aoc::game::Unit& addUnitAt(World& world, aoc::PlayerId owner, aoc::UnitTypeId typeId,
                                  int32_t q, int32_t r) {
    return world.gameState.player(owner)->addUnit(typeId, {q, r});
}

} // namespace aoc::test
