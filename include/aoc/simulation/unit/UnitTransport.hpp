#pragma once

/**
 * @file UnitTransport.hpp
 * @brief Naval units carrying land units.
 *
 * `UnitComponent::cargoCapacity` and `cargo` were written at unit creation and
 * never read: the serializer hardcoded 0 with a comment saying so, and there
 * was no load or unload function anywhere. The game used self-embarkation
 * instead -- a land unit crosses water on its own.
 *
 * Self-embarkation stays: it is the fallback for a unit with no ride. A
 * transport is the faster, safer alternative, and the thing the capacity
 * column was always for.
 *
 * A passenger is NOT stored as a reference. Units carry no stable identity in
 * this object model -- EntityId is a flat index across every player's unit
 * vector, which shifts the moment any unit is removed -- so a stored handle
 * would rot. Instead a passenger is an embarked land unit standing on the
 * transport's own tile, which is a fact the save format already records
 * through each unit's own position and state. Nothing new is persisted.
 */

#include "aoc/core/ErrorCodes.hpp"
#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"

#include <cstdint>
#include <vector>

namespace aoc::game {
class GameState;
class Player;
class Unit;
} // namespace aoc::game
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

/// How many land units a naval unit of this type can carry. 0 = none.
[[nodiscard]] int32_t transportCapacity(UnitTypeId typeId);

/// True when `unit` can carry passengers at all.
[[nodiscard]] bool isTransport(const aoc::game::Unit& unit);

/// The embarked land units riding on `transport`: same owner, same tile.
[[nodiscard]] std::vector<aoc::game::Unit*> unitsAboard(aoc::game::Player& owner,
                                                        const aoc::game::Unit& transport);

/// Free passenger places on `transport`.
[[nodiscard]] int32_t freeTransportSlots(aoc::game::Player& owner,
                                         const aoc::game::Unit& transport);

/// Load the land unit at `unitAt` onto the transport at `transportAt`.
///
/// Both must belong to `player`, be adjacent or co-located, and the transport
/// must have a free place. The passenger moves onto the transport's tile and
/// becomes Embarked.
[[nodiscard]] ErrorCode requestLoadUnit(aoc::game::GameState& gameState,
                                        const aoc::map::HexGrid& grid, PlayerId player,
                                        hex::AxialCoord unitAt, hex::AxialCoord transportAt);

/// Put one passenger ashore from the transport at `transportAt` onto `landAt`,
/// which must be adjacent, passable land, and unoccupied.
[[nodiscard]] ErrorCode requestUnloadUnit(aoc::game::GameState& gameState,
                                          const aoc::map::HexGrid& grid, PlayerId player,
                                          hex::AxialCoord transportAt, hex::AxialCoord landAt);

/// Carry every passenger along with a transport that has moved.
/// Called from the movement step; passengers ride, they do not swim after.
void carryPassengers(aoc::game::Player& owner, const aoc::game::Unit& transport,
                     hex::AxialCoord from, hex::AxialCoord to);

/// Everyone aboard a sunk transport drowns with it.
void drownPassengers(aoc::game::Player& owner, const aoc::game::Unit& transport);

} // namespace aoc::sim
