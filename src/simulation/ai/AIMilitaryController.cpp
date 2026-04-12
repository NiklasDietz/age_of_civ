/**
 * @file AIMilitaryController.cpp
 * @brief AI military and scout unit management: combat, defense, patrol, and exploration.
 */

#include "aoc/simulation/ai/AIMilitaryController.hpp"

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/unit/Movement.hpp"
#include "aoc/simulation/unit/Combat.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"

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
    aoc::game::Player* gsPlayer = gameState.player(this->m_player);
    if (gsPlayer == nullptr) {
        return;
    }

    // Snapshot positions of combat-eligible units owned by this player.
    // We copy position/type data so we don't hold live pointers across combat
    // calls that may remove units from the player's list.
    struct OwnedUnitSnapshot {
        hex::AxialCoord position;
        UnitTypeId      typeId;
    };
    std::vector<OwnedUnitSnapshot> ownedSnapshots;
    for (const std::unique_ptr<aoc::game::Unit>& unitPtr : gsPlayer->units()) {
        const UnitTypeDef& def = unitTypeDef(unitPtr->typeId());
        if (isMilitary(def.unitClass) || def.unitClass == UnitClass::Scout) {
            ownedSnapshots.push_back({unitPtr->position(), unitPtr->typeId()});
        }
    }

    // Cache enemy unit positions across all other players.
    struct EnemyUnitSnapshot {
        hex::AxialCoord position;
        PlayerId        owner;
    };
    std::vector<EnemyUnitSnapshot> enemySnapshots;
    for (const std::unique_ptr<aoc::game::Player>& otherPlayer : gameState.players()) {
        if (otherPlayer->id() == this->m_player) { continue; }
        for (const std::unique_ptr<aoc::game::Unit>& unitPtr : otherPlayer->units()) {
            enemySnapshots.push_back({unitPtr->position(), otherPlayer->id()});
        }
    }

    // Cache own city locations for defense decisions.
    std::vector<hex::AxialCoord> ownCityLocations;
    for (const std::unique_ptr<aoc::game::City>& city : gsPlayer->cities()) {
        ownCityLocations.push_back(city->location());
    }

    // Cache enemy city locations for offensive targeting.
    struct EnemyCitySnapshot {
        hex::AxialCoord position;
        PlayerId        owner;
    };
    std::vector<EnemyCitySnapshot> enemyCities;
    for (const std::unique_ptr<aoc::game::Player>& otherPlayer : gameState.players()) {
        if (otherPlayer->id() == this->m_player) { continue; }
        for (const std::unique_ptr<aoc::game::City>& city : otherPlayer->cities()) {
            enemyCities.push_back({city->location(), otherPlayer->id()});
        }
    }

    for (const OwnedUnitSnapshot& snap : ownedSnapshots) {
        // Re-look up the live unit by position — it may have been killed or moved.
        aoc::game::Unit* unit = gsPlayer->unitAt(snap.position);
        if (unit == nullptr) { continue; }
        if (unit->movementRemaining() <= 0) { continue; }

        const UnitTypeDef& def = unitTypeDef(unit->typeId());
        const std::array<hex::AxialCoord, 6> neighborTiles = hex::neighbors(unit->position());

        // ================================================================
        // SCOUTS: Move toward least-explored territory.
        // ================================================================
        if (def.unitClass == UnitClass::Scout) {
            hex::AxialCoord bestMove = unit->position();
            int32_t lowestOwnedCount = std::numeric_limits<int32_t>::max();

            for (const hex::AxialCoord& nbr : neighborTiles) {
                if (!grid.isValid(nbr) || grid.movementCost(grid.toIndex(nbr)) <= 0) { continue; }
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
                    bestMove         = nbr;
                }
            }

            if (bestMove != unit->position()) {
                orderUnitMove(*unit, bestMove, grid);
                moveUnitAlongPath(gameState, *unit, grid);
            }
            continue;
        }

        // ================================================================
        // MILITARY UNITS: Attack enemies, defend cities, patrol borders.
        // ================================================================
        if (!isMilitary(def.unitClass)) { continue; }

        // Ranged attack: find nearest target within range.
        if (def.rangedStrength > 0 && def.range > 0) {
            const EnemyUnitSnapshot* bestTarget = nullptr;
            int32_t bestTargetDist = std::numeric_limits<int32_t>::max();

            for (const EnemyUnitSnapshot& enemy : enemySnapshots) {
                const int32_t dist = hex::distance(unit->position(), enemy.position);
                if (dist <= def.range && dist < bestTargetDist) {
                    bestTargetDist = dist;
                    bestTarget     = &enemy;
                }
            }

            if (bestTarget != nullptr) {
                aoc::game::Player* enemyPlayer = gameState.player(bestTarget->owner);
                if (enemyPlayer != nullptr) {
                    aoc::game::Unit* targetUnit = enemyPlayer->unitAt(bestTarget->position);
                    if (targetUnit != nullptr) {
                        resolveRangedCombat(gameState, rng, grid, *unit, *targetUnit);
                        // Re-look up our unit — it may have been killed by ZoC/retaliation.
                        unit = gsPlayer->unitAt(snap.position);
                        if (unit == nullptr) { continue; }
                    }
                }
            }
        }

        // Melee attack: strike any adjacent enemy unit.
        bool attacked = false;
        if (def.rangedStrength == 0 || def.range == 0) {
            for (const hex::AxialCoord& nbr : neighborTiles) {
                if (!grid.isValid(nbr)) { continue; }
                for (const EnemyUnitSnapshot& enemy : enemySnapshots) {
                    if (enemy.position != nbr) { continue; }
                    aoc::game::Player* enemyPlayer = gameState.player(enemy.owner);
                    if (enemyPlayer == nullptr) { continue; }
                    aoc::game::Unit* targetUnit = enemyPlayer->unitAt(enemy.position);
                    if (targetUnit == nullptr) { continue; }

                    resolveMeleeCombat(gameState, rng, grid, *unit, *targetUnit);
                    attacked = true;
                    // Re-look up our unit — we may have died.
                    unit = gsPlayer->unitAt(snap.position);
                    break;
                }
                if (attacked) { break; }
            }
        }

        if (attacked || unit == nullptr) { continue; }

        // Defend: rush toward any city threatened by an enemy within 3 tiles.
        bool cityThreatened = false;
        hex::AxialCoord threatenedCityPos{};
        for (const hex::AxialCoord& cityLoc : ownCityLocations) {
            for (const EnemyUnitSnapshot& enemy : enemySnapshots) {
                if (hex::distance(cityLoc, enemy.position) <= 3) {
                    cityThreatened    = true;
                    threatenedCityPos = cityLoc;
                    break;
                }
            }
            if (cityThreatened) { break; }
        }

        if (cityThreatened) {
            const int32_t distToCity = hex::distance(unit->position(), threatenedCityPos);
            if (distToCity > 1) {
                orderUnitMove(*unit, threatenedCityPos, grid);
                moveUnitAlongPath(gameState, *unit, grid);
            }
            continue;
        }

        // Seek nearest enemy unit or city.
        int32_t closestDist = std::numeric_limits<int32_t>::max();
        hex::AxialCoord closestTarget = unit->position();
        const bool hardMode = (this->m_difficulty == aoc::ui::AIDifficulty::Hard);

        if (hardMode) {
            for (const EnemyCitySnapshot& ecity : enemyCities) {
                const int32_t dist = hex::distance(unit->position(), ecity.position);
                if (dist < closestDist) {
                    closestDist   = dist;
                    closestTarget = ecity.position;
                }
            }
        }

        for (const EnemyUnitSnapshot& enemy : enemySnapshots) {
            const int32_t dist = hex::distance(unit->position(), enemy.position);
            if (dist < closestDist) {
                closestDist   = dist;
                closestTarget = enemy.position;
            }
        }

        if (!hardMode) {
            for (const EnemyCitySnapshot& ecity : enemyCities) {
                const int32_t dist = hex::distance(unit->position(), ecity.position);
                if (dist < closestDist) {
                    closestDist   = dist;
                    closestTarget = ecity.position;
                }
            }
        }

        if (closestDist < std::numeric_limits<int32_t>::max() && closestDist > 1) {
            hex::AxialCoord bestMove = unit->position();
            int32_t bestMoveDist    = closestDist;
            for (const hex::AxialCoord& nbr : neighborTiles) {
                if (!grid.isValid(nbr) || grid.movementCost(grid.toIndex(nbr)) <= 0) { continue; }
                const int32_t dist = hex::distance(nbr, closestTarget);
                if (dist < bestMoveDist) {
                    bestMoveDist = dist;
                    bestMove     = nbr;
                }
            }
            if (bestMove != unit->position()) {
                orderUnitMove(*unit, bestMove, grid);
                moveUnitAlongPath(gameState, *unit, grid);
            }
            continue;
        }

        // No enemies visible — patrol the nearest border tile.
        if (closestDist == std::numeric_limits<int32_t>::max()) {
            hex::AxialCoord bestBorder    = unit->position();
            int32_t bestBorderScore       = std::numeric_limits<int32_t>::min();

            std::vector<hex::AxialCoord> searchTiles;
            searchTiles.reserve(60);
            hex::spiral(unit->position(), 4, std::back_inserter(searchTiles));

            for (const hex::AxialCoord& tile : searchTiles) {
                if (!grid.isValid(tile)) { continue; }
                const int32_t tileIdx = grid.toIndex(tile);
                if (grid.owner(tileIdx) != this->m_player) { continue; }
                if (grid.movementCost(tileIdx) <= 0) { continue; }

                const std::array<hex::AxialCoord, 6> tileNbrs = hex::neighbors(tile);
                int32_t unownedNeighbors = 0;
                for (const hex::AxialCoord& tn : tileNbrs) {
                    if (!grid.isValid(tn) || grid.owner(grid.toIndex(tn)) != this->m_player) {
                        ++unownedNeighbors;
                    }
                }
                if (unownedNeighbors == 0) { continue; }

                int32_t borderScore = unownedNeighbors * 10;
                const aoc::map::FeatureType feat = grid.feature(tileIdx);
                if (feat == aoc::map::FeatureType::Hills)  { borderScore += 5; }
                if (feat == aoc::map::FeatureType::Forest) { borderScore += 3; }
                borderScore -= hex::distance(unit->position(), tile);

                if (borderScore > bestBorderScore) {
                    bestBorderScore = borderScore;
                    bestBorder      = tile;
                }
            }

            if (bestBorder != unit->position()) {
                orderUnitMove(*unit, bestBorder, grid);
                moveUnitAlongPath(gameState, *unit, grid);
            } else {
                const int32_t unitIdx = grid.toIndex(unit->position());
                const aoc::map::FeatureType unitFeat = grid.feature(unitIdx);
                if (unitFeat == aoc::map::FeatureType::Hills ||
                    unitFeat == aoc::map::FeatureType::Forest) {
                    if (unit->state() != UnitState::Fortified) {
                        unit->setState(UnitState::Fortified);
                        LOG_INFO("AI %u Unit fortified at (%d,%d)",
                                 static_cast<unsigned>(this->m_player),
                                 unit->position().q, unit->position().r);
                    }
                } else {
                    std::vector<hex::AxialCoord> validMoves;
                    for (const hex::AxialCoord& nbr : neighborTiles) {
                        if (grid.isValid(nbr) && grid.movementCost(grid.toIndex(nbr)) > 0) {
                            validMoves.push_back(nbr);
                        }
                    }
                    if (!validMoves.empty()) {
                        const int32_t idx = rng.nextInt(
                            0, static_cast<int32_t>(validMoves.size()) - 1);
                        orderUnitMove(*unit, validMoves[static_cast<std::size_t>(idx)], grid);
                        moveUnitAlongPath(gameState, *unit, grid);
                    }
                }
            }
        }
    }
}

} // namespace aoc::sim::ai
