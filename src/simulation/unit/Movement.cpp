/**
 * @file Movement.cpp
 * @brief Unit movement implementation with stacking rules and zone of control.
 */

#include "aoc/simulation/unit/Movement.hpp"
#include "aoc/simulation/event/VisibilityEvents.hpp"
#include "aoc/simulation/city/CityBombardment.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/map/Pathfinding.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/core/Log.hpp"

#include <array>
#include <optional>
#include <vector>
#include <memory>

namespace aoc::sim {

// ============================================================================
// Stacking helpers
// ============================================================================

/**
 * @brief Check whether a unit may legally occupy a tile given stacking rules.
 *
 * Only one military unit and one civilian unit of the same player may occupy
 * the same tile simultaneously. A unit that is itself already on the tile
 * does not block itself.
 */
static bool canOccupyTile(const aoc::game::GameState& gameState,
                           aoc::hex::AxialCoord tile,
                           PlayerId owner,
                           bool isMilitaryUnit,
                           const aoc::game::Unit* movingUnit) {
    const aoc::game::Player* ownerPlayer = gameState.player(owner);
    if (ownerPlayer == nullptr) {
        return true;
    }

    for (const std::unique_ptr<aoc::game::Unit>& other : ownerPlayer->units()) {
        if (other.get() == movingUnit) {
            continue;
        }
        if (!(other->position() == tile)) {
            continue;
        }
        const bool otherIsMilitary = other->isMilitary();
        if (otherIsMilitary == isMilitaryUnit) {
            return false;  // Same classification already occupies this tile
        }
    }
    return true;
}

// ============================================================================
// Zone of control helpers
// ============================================================================

/**
 * @brief Check if a tile is in an enemy military unit's zone of control.
 *
 * A tile is in enemy ZoC if any of its 6 hex neighbours is occupied by an
 * enemy military unit belonging to a player other than movingPlayer.
 */
static bool isInEnemyZoneOfControl(const aoc::game::GameState& gameState,
                                    aoc::hex::AxialCoord tile,
                                    PlayerId movingPlayer) {
    const std::array<aoc::hex::AxialCoord, 6> nbrs = aoc::hex::neighbors(tile);

    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        if (player->id() == movingPlayer) {
            continue;
        }
        for (const std::unique_ptr<aoc::game::Unit>& unit : player->units()) {
            if (!unit->isMilitary()) {
                continue;
            }
            for (const aoc::hex::AxialCoord& nbr : nbrs) {
                if (unit->position() == nbr) {
                    return true;
                }
            }
        }
    }
    return false;
}

// ============================================================================
// Movement
// ============================================================================

bool moveUnitAlongPath(aoc::game::GameState& gameState, aoc::game::Unit& unit,
                        const aoc::map::HexGrid& grid) {
    if (unit.pendingPath().empty()) {
        return false;
    }

    const bool unitIsMilitary = unit.isMilitary();

    bool moved = false;

    while (!unit.pendingPath().empty() && unit.movementRemaining() > 0) {
        const aoc::hex::AxialCoord nextTile = unit.pendingPath().front();

        if (!grid.isValid(nextTile)) {
            unit.clearPath();
            break;
        }

        // Determine movement cost based on unit class and current state
        const int32_t tileIndex = grid.toIndex(nextTile);
        int32_t cost = 0;
        if (unit.isNaval()) {
            cost = grid.navalMovementCost(tileIndex);
        } else if (unit.state() == aoc::sim::UnitState::Embarked) {
            // Embarked land units may only traverse coast tiles. Any other
            // water class (Ocean, Shallow, Lake) is impassable for embarked
            // movement — the unit must wait for the carrying naval unit to
            // ferry it. Explicit reject makes the guard unambiguous.
            const aoc::map::TerrainType terrain = grid.terrain(tileIndex);
            if (terrain == aoc::map::TerrainType::Coast) {
                cost = 2;
            } else {
                cost = 0;  // explicit: anything else blocks the step
            }
        } else {
            cost = grid.movementCost(tileIndex);
        }

        if (cost == 0) {
            // Tile is impassable for this unit; invalidate the stored path
            unit.clearPath();
            break;
        }

        if (unit.movementRemaining() < cost) {
            // Insufficient movement this turn; resume next turn
            break;
        }

        // Stacking check: no two units of the same classification per player per tile
        if (!canOccupyTile(gameState, nextTile, unit.owner(), unitIsMilitary, &unit)) {
            unit.clearPath();
            break;
        }

        // Advance the unit; record animation data for smooth interpolation
        const aoc::hex::AxialCoord oldPosition = unit.position();
        unit.setPosition(nextTile);
        unit.setMovementRemaining(unit.movementRemaining() - cost);
        unit.pendingPath().erase(unit.pendingPath().begin());
        moved = true;

        unit.isAnimating  = true;
        unit.animProgress = 0.0f;
        unit.animFrom     = oldPosition;
        unit.animTo       = nextTile;

        if (unitIsMilitary) {
            aoc::sim::VisibilityEvent ev{};
            ev.type = aoc::sim::VisibilityEventType::EnemyUnitSpotted;
            ev.location = nextTile;
            ev.actor = unit.owner();
            ev.payload = static_cast<int32_t>(unit.typeId().value);
            gameState.visibilityBus().emit(ev);
        }

        // City capture: a military unit stepping onto an enemy city captures it
        // ONLY if its walls are breached (or it has no walls). Cities with
        // intact walls cannot be walked over — the unit stops on the
        // approach tile and must continue to siege the walls first.
        // A previously captured city is not removed from the original owner's
        // `m_cities` list, so `cityAt` can still find it there.  We must check
        // the city's current owner before re-capturing to avoid logging (and
        // re-firing setOwner on) a city that is already ours.
        bool wallsBlockedStep = false;
        if (unitIsMilitary) {
            for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
                if (player->id() == unit.owner()) {
                    continue;
                }
                aoc::game::City* city = player->cityAt(nextTile);
                if (city == nullptr) {
                    continue;
                }
                if (city->owner() == unit.owner()) {
                    continue;  // Already captured — stale entry in old owner's list.
                }
                if (!canCaptureCity(*city)) {
                    LOG_INFO("Unit blocked by intact walls of %s (%d/%d HP)",
                             city->name().c_str(),
                             city->walls().currentHP, city->walls().maxHP);
                    unit.clearPath();
                    unit.setMovementRemaining(0);
                    wallsBlockedStep = true;
                    break;
                }

                const PlayerId previousOwner = city->owner();
                city->setOwner(unit.owner());
                if (city->population() > 1) {
                    city->setPopulation(city->population() - 1);
                }

                // Clear the captured city's production queue
                city->production().queue.clear();

                // Transfer tile ownership for worked tiles
                for (const aoc::hex::AxialCoord& workedTile : city->workedTiles()) {
                    if (grid.isValid(workedTile)) {
                        const int32_t wtIndex = grid.toIndex(workedTile);
                        if (grid.owner(wtIndex) == previousOwner) {
                            const_cast<aoc::map::HexGrid&>(grid).setOwner(wtIndex, unit.owner());
                        }
                    }
                }

                unit.setMovementRemaining(0);
                LOG_INFO("City %s captured by player %u (was player %u)",
                         city->name().c_str(),
                         static_cast<unsigned>(unit.owner()),
                         static_cast<unsigned>(previousOwner));
                break;
            }
        }
        if (wallsBlockedStep) { break; }

        // Zone of control: stop as soon as the unit enters an enemy ZoC tile.
        // Previously only blocked ZoC->ZoC transitions, which let units pass
        // through an enemy screening line in a single step if they approached
        // from open terrain. Blocking entry outright restores the screening
        // guarantee defensive ZoC is meant to provide.
        if (isInEnemyZoneOfControl(gameState, nextTile, unit.owner())) {
            unit.setMovementRemaining(0);
            break;
        }
    }

    unit.setState(unit.pendingPath().empty() ? aoc::sim::UnitState::Idle
                                             : aoc::sim::UnitState::Moving);
    return moved;
}

bool orderUnitMove(aoc::game::Unit& unit,
                    aoc::hex::AxialCoord goal, const aoc::map::HexGrid& grid) {
    if (unit.position() == goal) {
        unit.clearPath();
        return true;
    }

    const bool navalPath = unit.isNaval();

    const std::optional<aoc::map::PathResult> pathResult = aoc::map::findPath(
        grid, unit.position(), goal, 0, nullptr, INVALID_PLAYER, navalPath);

    if (!pathResult.has_value()) {
        return false;
    }

    // Store path excluding the starting tile (first element equals current position)
    unit.clearPath();
    if (pathResult->path.size() > 1) {
        unit.pendingPath().assign(
            pathResult->path.begin() + 1,
            pathResult->path.end());
    }
    unit.setState(aoc::sim::UnitState::Moving);

    return true;
}

void refreshMovement(aoc::game::GameState& gameState, PlayerId player) {
    aoc::game::Player* playerObj = gameState.player(player);
    if (playerObj == nullptr) {
        return;
    }

    for (const std::unique_ptr<aoc::game::Unit>& unit : playerObj->units()) {
        unit->refreshMovement();
    }
}

void executeMovement(aoc::game::GameState& gameState, PlayerId player,
                      const aoc::map::HexGrid& grid) {
    aoc::game::Player* playerObj = gameState.player(player);
    if (playerObj == nullptr) {
        return;
    }

    // Collect raw pointers first; the vector itself is not modified during iteration,
    // but clearPath / setPosition mutate the units, which is safe through raw pointers.
    std::vector<aoc::game::Unit*> pendingUnits;
    for (const std::unique_ptr<aoc::game::Unit>& unit : playerObj->units()) {
        if (!unit->pendingPath().empty()) {
            pendingUnits.push_back(unit.get());
        }
    }

    for (aoc::game::Unit* unit : pendingUnits) {
        moveUnitAlongPath(gameState, *unit, grid);
    }
}

// ============================================================================
// EntityId overloads -- find the Unit by searching all players' unit lists.
// The EntityId is used as a lookup key via position+typeId matching against
// the legacy UnitComponent stored on the same entity.
// ============================================================================

/**
 * @brief Find a Unit in the GameState object model corresponding to the given
 *        legacy EntityId.
 *
 * The EntityId encodes an index that corresponds to the UnitComponent in the
 * legacy ECS pool. We resolve it by matching the unit's serial index across
 * all players' unit vectors: the n-th unit overall maps to EntityId{n}.
 * This preserves the EntityId contract without touching the ECS World.
 */
static aoc::game::Unit* findUnitByEntity(aoc::game::GameState& gameState, EntityId entity) {
    uint32_t remaining = entity.index;
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        const std::vector<std::unique_ptr<aoc::game::Unit>>& units = playerPtr->units();
        const uint32_t count = static_cast<uint32_t>(units.size());
        if (remaining < count) {
            return units[static_cast<std::size_t>(remaining)].get();
        }
        remaining -= count;
    }
    return nullptr;
}

bool moveUnitAlongPath(aoc::game::GameState& gameState, EntityId unitEntity,
                       const aoc::map::HexGrid& grid) {
    aoc::game::Unit* unit = findUnitByEntity(gameState, unitEntity);
    if (unit == nullptr) { return false; }
    return moveUnitAlongPath(gameState, *unit, grid);
}

bool orderUnitMove(aoc::game::GameState& gameState, EntityId unitEntity,
                   aoc::hex::AxialCoord goal, const aoc::map::HexGrid& grid) {
    aoc::game::Unit* unit = findUnitByEntity(gameState, unitEntity);
    if (unit == nullptr) { return false; }
    return orderUnitMove(*unit, goal, grid);
}

} // namespace aoc::sim
