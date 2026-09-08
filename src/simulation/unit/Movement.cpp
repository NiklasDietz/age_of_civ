/**
 * @file Movement.cpp
 * @brief Unit movement implementation with stacking rules and zone of control.
 */

#include "aoc/simulation/unit/UnitTransport.hpp"
#include "aoc/simulation/unit/Movement.hpp"
#include "aoc/simulation/event/VisibilityEvents.hpp"
#include "aoc/simulation/city/CityBombardment.hpp"
#include "aoc/simulation/city/CitySiege.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/game/ZoneOfControl.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/map/Pathfinding.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
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
    // Another seat's unit blocks a military mover outright; attacks go through
    // requestAttack. Civilians keep passing (Traders, Missionaries and Spies
    // enter foreign cities). Until 2026-09-05 only the mover's own units counted.
    if (!isMilitaryUnit) {
        return true;
    }
    for (const std::unique_ptr<aoc::game::Player>& seat : gameState.players()) {
        if (seat == nullptr || seat->id() == owner) { continue; }
        if (seat->unitAt(tile) != nullptr) { return false; }
    }
    for (const std::unique_ptr<aoc::game::Player>& seat : gameState.cityStatePlayers()) {
        if (seat == nullptr || seat->id() == owner) { continue; }
        if (seat->unitAt(tile) != nullptr) { return false; }
    }
    if (const aoc::game::Player* barbarians = gameState.barbarianPlayer();
        barbarians != nullptr && barbarians->id() != owner && barbarians->unitAt(tile) != nullptr) {
        return false;
    }
    return true;
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
            // 2026-05-03: Navigation tech (id 30) gates Ocean for naval units
            // too. Without it, ships are coast/shallow-water only — matches
            // the same rule applied to embarked land units.
            aoc::game::Player* navOwner = gameState.player(unit.owner());
            const bool hasNavigation = navOwner != nullptr
                && navOwner->tech().hasResearched(aoc::TechId{30});
            cost = hasNavigation
                ? grid.navalMovementCost(tileIndex)
                : grid.shallowNavalMovementCost(tileIndex);
        } else if (unit.state() == aoc::sim::UnitState::Embarked) {
            // 2026-05-03: embarked land units traverse Coast and ShallowWater
            // freely (canoes, coastal sailing). Ocean tiles require Navigation
            // tech (TechId 30). Lakes / impassable still blocked.
            const aoc::map::TerrainType terrain = grid.terrain(tileIndex);
            if (terrain == aoc::map::TerrainType::Coast
                || terrain == aoc::map::TerrainType::ShallowWater) {
                cost = 2;
            } else if (terrain == aoc::map::TerrainType::Ocean) {
                aoc::game::Player* navOwner = gameState.player(unit.owner());
                const bool hasNavigation = navOwner != nullptr
                    && navOwner->tech().hasResearched(aoc::TechId{30});
                cost = hasNavigation ? 3 : 0;
            } else {
                cost = 0;  // Lakes / other water types still blocked
            }
            if (grid.feature(tileIndex) == aoc::map::FeatureType::Ice) {
                cost = 0;  // ice shelves block embarked units like ships
            }
        } else {
            // Directional cost: a river edge costs +1 (the one-arg overload
            // ignored rivers, so crossings were free until 2026-09-05).
            cost = grid.movementCost(grid.toIndex(unit.position()), tileIndex);
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
        // Anyone riding this hull goes with it. Without this a transport sailed
        // out from under its passengers, who were left treading water on the
        // tile it left.
        if (aoc::sim::isTransport(unit)) {
            aoc::game::Player* carrier = gameState.player(unit.owner());
            if (carrier != nullptr) {
                aoc::sim::carryPassengers(*carrier, unit, oldPosition, nextTile);
            }
        }
        unit.setMovementRemaining(unit.movementRemaining() - cost);
        unit.pendingPath().erase(unit.pendingPath().begin());
        unit.movementTrace().push_back(nextTile);
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
                // Pressing into an enemy city grinds its walls, then its hit
                // points, and captures it when both are gone. Every side
                // effect lives in CitySiege.cpp so the melee attack and this
                // walk-in cannot drift apart.
                const aoc::sim::CityAttackResult siege = aoc::sim::pressIntoCity(
                    gameState, const_cast<aoc::map::HexGrid&>(grid), unit, *city,
                    gameState.currentTurn());
                if (!siege.captured) {
                    LOG_INFO("Siege: %.*s vs %s -- walls -%d (%d/%d), city -%d (%d/%d)",
                             static_cast<int>(unit.typeDef().name.size()),
                             unit.typeDef().name.data(), city->name().c_str(), siege.wallDamage,
                             city->walls().currentHP, city->walls().maxHP, siege.cityDamage,
                             city->combat().hp, city->combat().maxHP);
                    unit.clearPath();
                    unit.setMovementRemaining(0);
                    wallsBlockedStep = true;
                    break;
                }
                break;
            }
        }
        if (wallsBlockedStep) { break; }

        // Zone of control: stop as soon as the unit enters an enemy ZoC tile.
        // Previously only blocked ZoC->ZoC transitions, which let units pass
        // through an enemy screening line in a single step if they approached
        // from open terrain. Blocking entry outright restores the screening
        // guarantee defensive ZoC is meant to provide.
        //
        // Civilians (settlers, builders, traders) and embarked land units
        // bypass ZoC: otherwise a non-hostile neighbour's patrol would freeze
        // civilian travel across open terrain. Only military, non-embarked
        // units are subject to it; the shared test lives in aoc::game.
        if (unitIsMilitary
            && unit.state() != aoc::sim::UnitState::Embarked
            && aoc::game::isInEnemyZoneOfControl(gameState, nextTile, unit.owner())) {
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
