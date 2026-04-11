/**
 * @file AIMilitaryController.cpp
 * @brief AI military and scout unit management: combat, defense, patrol, and exploration.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/simulation/ai/AIMilitaryController.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/unit/Movement.hpp"
#include "aoc/simulation/unit/Combat.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/ecs/World.hpp"

#include <limits>
#include <vector>

namespace aoc::sim::ai {

// ============================================================================
// Constructor
// ============================================================================

AIMilitaryController::AIMilitaryController(PlayerId player, aoc::ui::AIDifficulty difficulty)
    : m_player(player)
    , m_difficulty(difficulty)
{
}

// ============================================================================
// Military and scout actions
// ============================================================================

void AIMilitaryController::executeMilitaryActions(aoc::game::GameState& gameState,
                                                   aoc::map::HexGrid& grid,
                                                   aoc::Random& rng) {
    aoc::ecs::World& world = gameState.legacyWorld();
    aoc::ecs::ComponentPool<UnitComponent>* unitPool = world.getPool<UnitComponent>();
    if (unitPool == nullptr) {
        return;
    }

    // Collect owned military and scout units (copy because combat may destroy entities)
    struct UnitInfo {
        EntityId entity;
        UnitComponent unit;
    };
    std::vector<UnitInfo> ownedUnits;
    for (uint32_t i = 0; i < unitPool->size(); ++i) {
        if (unitPool->data()[i].owner == this->m_player) {
            const UnitTypeDef& def = unitTypeDef(unitPool->data()[i].typeId);
            if (isMilitary(def.unitClass) || def.unitClass == UnitClass::Scout) {
                ownedUnits.push_back({unitPool->entities()[i], unitPool->data()[i]});
            }
        }
    }

    // Cache enemy unit positions
    struct EnemyInfo {
        hex::AxialCoord position;
        EntityId entity;
        PlayerId owner;
    };
    std::vector<EnemyInfo> enemyUnits;
    for (uint32_t i = 0; i < unitPool->size(); ++i) {
        if (unitPool->data()[i].owner != this->m_player) {
            enemyUnits.push_back({
                unitPool->data()[i].position,
                unitPool->entities()[i],
                unitPool->data()[i].owner
            });
        }
    }

    // Cache own city locations for defense
    std::vector<hex::AxialCoord> ownCityLocations;
    const aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    if (cityPool != nullptr) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            if (cityPool->data()[i].owner == this->m_player) {
                ownCityLocations.push_back(cityPool->data()[i].location);
            }
        }
    }

    // Cache enemy city locations for military targeting
    struct EnemyCityInfo {
        hex::AxialCoord position;
        PlayerId owner;
    };
    std::vector<EnemyCityInfo> enemyCities;
    if (cityPool != nullptr) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            if (cityPool->data()[i].owner != this->m_player) {
                enemyCities.push_back({
                    cityPool->data()[i].location,
                    cityPool->data()[i].owner
                });
            }
        }
    }

    for (const UnitInfo& info : ownedUnits) {
        if (!world.isAlive(info.entity)) {
            continue;
        }

        const UnitTypeDef& def = unitTypeDef(info.unit.typeId);

        if (info.unit.movementRemaining <= 0) {
            continue;
        }

        const std::array<hex::AxialCoord, 6> neighborTiles = hex::neighbors(info.unit.position);

        // ================================================================
        // SCOUTS: Explore toward unexplored territory
        // ================================================================
        if (def.unitClass == UnitClass::Scout) {
            hex::AxialCoord bestMove = info.unit.position;
            int32_t lowestOwnedCount = std::numeric_limits<int32_t>::max();

            for (const hex::AxialCoord& nbr : neighborTiles) {
                if (!grid.isValid(nbr) || grid.movementCost(grid.toIndex(nbr)) <= 0) {
                    continue;
                }
                int32_t ownedNearby = 0;
                std::vector<hex::AxialCoord> checkTiles;
                checkTiles.reserve(18);
                hex::spiral(nbr, 2, std::back_inserter(checkTiles));
                for (const hex::AxialCoord& ct : checkTiles) {
                    if (grid.isValid(ct) && grid.owner(grid.toIndex(ct)) == this->m_player) {
                        ++ownedNearby;
                    }
                }
                if (ownedNearby < lowestOwnedCount) {
                    lowestOwnedCount = ownedNearby;
                    bestMove = nbr;
                }
            }

            if (bestMove != info.unit.position) {
                orderUnitMove(world, info.entity, bestMove, grid);
                moveUnitAlongPath(world, info.entity, grid);
            }
            continue;
        }

        // ================================================================
        // MILITARY UNITS: Attack, defend, patrol
        // ================================================================
        if (isMilitary(def.unitClass)) {
            // Ranged attack: try to attack enemies within range first
            if (def.rangedStrength > 0 && def.range > 0) {
                EntityId bestTarget = NULL_ENTITY;
                int32_t bestTargetDist = std::numeric_limits<int32_t>::max();

                for (const EnemyInfo& enemy : enemyUnits) {
                    const int32_t dist = hex::distance(info.unit.position, enemy.position);
                    if (dist <= def.range && dist < bestTargetDist) {
                        bestTargetDist = dist;
                        bestTarget = enemy.entity;
                    }
                }

                if (bestTarget.isValid() && world.isAlive(bestTarget)) {
                    resolveRangedCombat(world, rng, grid, info.entity, bestTarget);
                    if (!world.isAlive(info.entity)) {
                        continue;
                    }
                }
            }

            // Melee attack: check for adjacent enemies
            bool attacked = false;
            if (def.rangedStrength == 0 || def.range == 0) {
                for (const hex::AxialCoord& nbr : neighborTiles) {
                    if (!grid.isValid(nbr)) {
                        continue;
                    }
                    aoc::ecs::ComponentPool<UnitComponent>* pool =
                        world.getPool<UnitComponent>();
                    if (pool == nullptr) {
                        break;
                    }
                    for (uint32_t j = 0; j < pool->size(); ++j) {
                        if (pool->data()[j].position == nbr &&
                            pool->data()[j].owner != this->m_player) {
                            resolveMeleeCombat(world, rng, grid, info.entity, pool->entities()[j]);
                            attacked = true;
                            break;
                        }
                    }
                    if (attacked) {
                        break;
                    }
                }
            }

            if (attacked || !world.isAlive(info.entity)) {
                continue;
            }

            // Check if any own city is threatened (enemy within 3 tiles)
            bool cityThreatened = false;
            hex::AxialCoord threatenedCityPos{};
            for (const hex::AxialCoord& cityLoc : ownCityLocations) {
                for (const EnemyInfo& enemy : enemyUnits) {
                    if (hex::distance(cityLoc, enemy.position) <= 3) {
                        cityThreatened = true;
                        threatenedCityPos = cityLoc;
                        break;
                    }
                }
                if (cityThreatened) {
                    break;
                }
            }

            // Rush to defend threatened city
            if (cityThreatened) {
                const int32_t distToCity = hex::distance(info.unit.position, threatenedCityPos);
                if (distToCity > 1) {
                    orderUnitMove(world, info.entity, threatenedCityPos, grid);
                    moveUnitAlongPath(world, info.entity, grid);
                    continue;
                }
            }

            // Seek enemies: move toward nearest enemy unit or city
            int32_t closestDist = std::numeric_limits<int32_t>::max();
            hex::AxialCoord closestTarget = info.unit.position;

            const bool hardMode = (this->m_difficulty == aoc::ui::AIDifficulty::Hard);

            // Hard AI: prioritize cities first, then units
            if (hardMode) {
                for (const EnemyCityInfo& ecity : enemyCities) {
                    const int32_t dist = hex::distance(info.unit.position, ecity.position);
                    if (dist < closestDist) {
                        closestDist = dist;
                        closestTarget = ecity.position;
                    }
                }
            }

            for (const EnemyInfo& enemy : enemyUnits) {
                const int32_t dist = hex::distance(info.unit.position, enemy.position);
                if (dist < closestDist) {
                    closestDist = dist;
                    closestTarget = enemy.position;
                }
            }

            // Non-hard AI also considers enemy cities (after units)
            if (!hardMode) {
                for (const EnemyCityInfo& ecity : enemyCities) {
                    const int32_t dist = hex::distance(info.unit.position, ecity.position);
                    if (dist < closestDist) {
                        closestDist = dist;
                        closestTarget = ecity.position;
                    }
                }
            }

            if (closestDist < std::numeric_limits<int32_t>::max() && closestDist > 1) {
                hex::AxialCoord bestMove = info.unit.position;
                int32_t bestDist = closestDist;
                for (const hex::AxialCoord& nbr : neighborTiles) {
                    if (!grid.isValid(nbr) || grid.movementCost(grid.toIndex(nbr)) <= 0) {
                        continue;
                    }
                    const int32_t dist = hex::distance(nbr, closestTarget);
                    if (dist < bestDist) {
                        bestDist = dist;
                        bestMove = nbr;
                    }
                }
                if (bestMove != info.unit.position) {
                    orderUnitMove(world, info.entity, bestMove, grid);
                    moveUnitAlongPath(world, info.entity, grid);
                }
            }
            // No enemies visible -- patrol borders
            else if (closestDist == std::numeric_limits<int32_t>::max()) {
                hex::AxialCoord bestBorder = info.unit.position;
                int32_t bestBorderScore = std::numeric_limits<int32_t>::min();

                std::vector<hex::AxialCoord> searchTiles;
                searchTiles.reserve(60);
                hex::spiral(info.unit.position, 4, std::back_inserter(searchTiles));

                for (const hex::AxialCoord& tile : searchTiles) {
                    if (!grid.isValid(tile)) {
                        continue;
                    }
                    const int32_t tileIdx = grid.toIndex(tile);
                    if (grid.owner(tileIdx) != this->m_player) {
                        continue;
                    }
                    if (grid.movementCost(tileIdx) <= 0) {
                        continue;
                    }

                    const std::array<hex::AxialCoord, 6> tileNbrs = hex::neighbors(tile);
                    int32_t unownedNeighbors = 0;
                    for (const hex::AxialCoord& tn : tileNbrs) {
                        if (!grid.isValid(tn) || grid.owner(grid.toIndex(tn)) != this->m_player) {
                            ++unownedNeighbors;
                        }
                    }
                    if (unownedNeighbors == 0) {
                        continue;
                    }

                    int32_t borderScore = unownedNeighbors * 10;
                    const aoc::map::FeatureType feat = grid.feature(tileIdx);
                    if (feat == aoc::map::FeatureType::Hills) {
                        borderScore += 5;
                    }
                    if (feat == aoc::map::FeatureType::Forest) {
                        borderScore += 3;
                    }
                    borderScore -= hex::distance(info.unit.position, tile);

                    if (borderScore > bestBorderScore) {
                        bestBorderScore = borderScore;
                        bestBorder = tile;
                    }
                }

                if (bestBorder != info.unit.position) {
                    orderUnitMove(world, info.entity, bestBorder, grid);
                    moveUnitAlongPath(world, info.entity, grid);
                } else {
                    // Fortify in place if on good terrain
                    const int32_t unitIdx = grid.toIndex(info.unit.position);
                    const aoc::map::FeatureType unitFeat = grid.feature(unitIdx);
                    if (unitFeat == aoc::map::FeatureType::Hills ||
                        unitFeat == aoc::map::FeatureType::Forest) {
                        UnitComponent* liveUnit =
                            world.tryGetComponent<UnitComponent>(info.entity);
                        if (liveUnit != nullptr && liveUnit->state != UnitState::Fortified) {
                            liveUnit->state = UnitState::Fortified;
                            LOG_INFO("AI %u Unit fortified at (%d,%d)",
                                     static_cast<unsigned>(this->m_player),
                                     info.unit.position.q, info.unit.position.r);
                        }
                    } else {
                        // Random patrol if nothing better to do
                        std::vector<hex::AxialCoord> validMoves;
                        for (const hex::AxialCoord& nbr : neighborTiles) {
                            if (grid.isValid(nbr) && grid.movementCost(grid.toIndex(nbr)) > 0) {
                                validMoves.push_back(nbr);
                            }
                        }
                        if (!validMoves.empty()) {
                            const int32_t idx = rng.nextInt(
                                0, static_cast<int32_t>(validMoves.size()) - 1);
                            orderUnitMove(world, info.entity,
                                          validMoves[static_cast<std::size_t>(idx)], grid);
                            moveUnitAlongPath(world, info.entity, grid);
                        }
                    }
                }
            }
            continue;
        }
    }
}

} // namespace aoc::sim::ai
