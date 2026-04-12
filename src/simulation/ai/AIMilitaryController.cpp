/**
 * @file AIMilitaryController.cpp
 * @brief AI military and scout unit management: threat assessment, combat,
 *        city defense, unit composition enforcement, and border patrol.
 *
 * Every turn this controller:
 *   1. Computes a per-player threat ratio from enemy units near own cities.
 *   2. Issues orders to each military unit: attack, close-in, defend, or patrol.
 *   3. Verifies minimum unit composition targets per era and queues production
 *      requests when the player falls below the minimums.
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
// Threat assessment helpers
// ============================================================================

/**
 * @brief Compute a threat ratio for this player.
 *
 * threat_ratio = sum(combat_strength of enemy units within 10 tiles of any
 *                   own city) / own_total_military_strength
 *
 * A ratio > 1.0 means the enemy has more power near our cities than we have
 * in our entire military. Values above 1.5 indicate a critical situation.
 *
 * @param gameState  Full game state.
 * @param player     The player being assessed.
 * @return Threat ratio (0.0 = safe, higher = more dangerous).
 */
[[nodiscard]] static float computeThreatRatio(const aoc::game::GameState& gameState,
                                               PlayerId player) {
    const aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) {
        return 0.0f;
    }

    // Cache own city locations for distance queries.
    std::vector<aoc::hex::AxialCoord> ownCityLocs;
    ownCityLocs.reserve(static_cast<std::size_t>(gsPlayer->cityCount()));
    for (const std::unique_ptr<aoc::game::City>& city : gsPlayer->cities()) {
        ownCityLocs.push_back(city->location());
    }

    // Sum enemy unit strength near own cities.
    float threatStrength = 0.0f;
    for (const std::unique_ptr<aoc::game::Player>& otherPlayer : gameState.players()) {
        if (otherPlayer->id() == player) {
            continue;
        }
        for (const std::unique_ptr<aoc::game::Unit>& unitPtr : otherPlayer->units()) {
            const aoc::sim::UnitTypeDef& def = aoc::sim::unitTypeDef(unitPtr->typeId());
            if (!aoc::sim::isMilitary(def.unitClass)) {
                continue;
            }
            for (const aoc::hex::AxialCoord& cityLoc : ownCityLocs) {
                if (aoc::hex::distance(unitPtr->position(), cityLoc) <= 10) {
                    threatStrength += static_cast<float>(def.combatStrength);
                    break;  // Count each enemy unit at most once.
                }
            }
        }
    }

    // Sum own military strength.
    float ownStrength = 0.0f;
    for (const std::unique_ptr<aoc::game::Unit>& unitPtr : gsPlayer->units()) {
        const aoc::sim::UnitTypeDef& def = aoc::sim::unitTypeDef(unitPtr->typeId());
        if (aoc::sim::isMilitary(def.unitClass)) {
            ownStrength += static_cast<float>(def.combatStrength);
        }
    }

    if (ownStrength <= 0.0f) {
        return 10.0f;  // No military at all -- treat as maximum threat.
    }

    return threatStrength / ownStrength;
}

// ============================================================================
// Minimum military unit composition by era
// ============================================================================

/**
 * @brief Return the minimum desired military unit count for the given era.
 *
 * Targets are per-player (not per-city), scaled by city count.
 *
 * | Era        | Units per city |
 * |------------|----------------|
 * | Ancient    | 2              |
 * | Classical+ | 3              |
 *
 * @param era       Player's current era index.
 * @param cityCount Number of cities owned.
 * @return Desired minimum military unit count.
 */
[[nodiscard]] static int32_t desiredMilitaryUnits(aoc::EraId era, int32_t cityCount) {
    const int32_t unitsPerCity = (era.value == 0) ? 2 : 3;
    return unitsPerCity * std::max(cityCount, 1);
}

// ============================================================================
// executeMilitaryActions
// ============================================================================

/**
 * @brief Process all military and scout units for this player for one turn.
 *
 * Unit order priority (military units):
 *   1. If an enemy is adjacent: attack the weakest adjacent enemy.
 *   2. If an enemy is within 3 tiles: move directly toward it.
 *   3. If a city is threatened (enemy within 3 tiles): move to defend it.
 *   4. Otherwise: patrol to the nearest border tile or explore.
 *
 * Scout units always seek the least-explored territory.
 *
 * Safety: all positions and types are snapshotted before combat.  After any
 * resolveMeleeCombat / resolveRangedCombat call the unit is re-looked-up by
 * position because it may have died.  Enemy snapshots use position-at-snapshot
 * time; stale entries are simply skipped when the live lookup returns nullptr.
 */
void AIMilitaryController::executeMilitaryActions(aoc::game::GameState& gameState,
                                                   aoc::map::HexGrid& grid,
                                                   aoc::Random& rng) {
    aoc::game::Player* gsPlayer = gameState.player(this->m_player);
    if (gsPlayer == nullptr) {
        return;
    }

    // ----------------------------------------------------------------
    // Threat assessment
    // ----------------------------------------------------------------
    const float threatRatio = computeThreatRatio(gameState, this->m_player);

    if (threatRatio > 1.5f) {
        LOG_INFO("AI %u CRITICAL threat ratio=%.2f -- prioritising defense",
                 static_cast<unsigned>(this->m_player),
                 static_cast<double>(threatRatio));
    } else if (threatRatio > 0.7f) {
        LOG_INFO("AI %u Elevated threat ratio=%.2f",
                 static_cast<unsigned>(this->m_player),
                 static_cast<double>(threatRatio));
    }

    // ----------------------------------------------------------------
    // Snapshot own military/scout units
    // ----------------------------------------------------------------
    struct OwnedUnitSnapshot {
        aoc::hex::AxialCoord position;
        UnitTypeId           typeId;
    };

    std::vector<OwnedUnitSnapshot> ownedSnapshots;
    ownedSnapshots.reserve(static_cast<std::size_t>(gsPlayer->unitCount()));
    for (const std::unique_ptr<aoc::game::Unit>& unitPtr : gsPlayer->units()) {
        const aoc::sim::UnitTypeDef& def = aoc::sim::unitTypeDef(unitPtr->typeId());
        if (aoc::sim::isMilitary(def.unitClass) || def.unitClass == aoc::sim::UnitClass::Scout) {
            ownedSnapshots.push_back({unitPtr->position(), unitPtr->typeId()});
        }
    }

    // ----------------------------------------------------------------
    // Snapshot enemy units
    // ----------------------------------------------------------------
    struct EnemyUnitSnapshot {
        aoc::hex::AxialCoord position;
        PlayerId             owner;
        int32_t              combatStrength;
    };

    std::vector<EnemyUnitSnapshot> enemySnapshots;
    for (const std::unique_ptr<aoc::game::Player>& otherPlayer : gameState.players()) {
        if (otherPlayer->id() == this->m_player) {
            continue;
        }
        for (const std::unique_ptr<aoc::game::Unit>& unitPtr : otherPlayer->units()) {
            const aoc::sim::UnitTypeDef& def = aoc::sim::unitTypeDef(unitPtr->typeId());
            enemySnapshots.push_back({unitPtr->position(), otherPlayer->id(), def.combatStrength});
        }
    }

    // ----------------------------------------------------------------
    // Cache own city locations
    // ----------------------------------------------------------------
    std::vector<aoc::hex::AxialCoord> ownCityLocs;
    ownCityLocs.reserve(static_cast<std::size_t>(gsPlayer->cityCount()));
    for (const std::unique_ptr<aoc::game::City>& city : gsPlayer->cities()) {
        ownCityLocs.push_back(city->location());
    }

    // ----------------------------------------------------------------
    // Cache enemy city locations (for aggressive targeting in hard mode)
    // ----------------------------------------------------------------
    struct EnemyCitySnapshot {
        aoc::hex::AxialCoord position;
        PlayerId             owner;
    };

    std::vector<EnemyCitySnapshot> enemyCities;
    for (const std::unique_ptr<aoc::game::Player>& otherPlayer : gameState.players()) {
        if (otherPlayer->id() == this->m_player) {
            continue;
        }
        for (const std::unique_ptr<aoc::game::City>& city : otherPlayer->cities()) {
            enemyCities.push_back({city->location(), otherPlayer->id()});
        }
    }

    const bool hardMode = (this->m_difficulty == aoc::ui::AIDifficulty::Hard);

    // ----------------------------------------------------------------
    // Issue orders for each snapshotted unit
    // ----------------------------------------------------------------
    for (const OwnedUnitSnapshot& snap : ownedSnapshots) {
        // Re-look-up the live unit by position -- it may have died or moved.
        aoc::game::Unit* unit = gsPlayer->unitAt(snap.position);
        if (unit == nullptr || unit->movementRemaining() <= 0) {
            continue;
        }

        const aoc::sim::UnitTypeDef& def = aoc::sim::unitTypeDef(unit->typeId());
        const std::array<aoc::hex::AxialCoord, 6> neighborTiles =
            aoc::hex::neighbors(unit->position());

        // ============================================================
        // SCOUTS: Move toward least-explored territory.
        // ============================================================
        if (def.unitClass == aoc::sim::UnitClass::Scout) {
            aoc::hex::AxialCoord bestMove    = unit->position();
            int32_t              lowestOwned = std::numeric_limits<int32_t>::max();

            for (const aoc::hex::AxialCoord& nbr : neighborTiles) {
                if (!grid.isValid(nbr) || grid.movementCost(grid.toIndex(nbr)) <= 0) {
                    continue;
                }
                int32_t ownedNearby = 0;
                std::vector<aoc::hex::AxialCoord> checkTiles;
                checkTiles.reserve(18);
                aoc::hex::spiral(nbr, 2, std::back_inserter(checkTiles));
                for (const aoc::hex::AxialCoord& ct : checkTiles) {
                    if (grid.isValid(ct) && grid.owner(grid.toIndex(ct)) == this->m_player) {
                        ++ownedNearby;
                    }
                }
                if (ownedNearby < lowestOwned) {
                    lowestOwned = ownedNearby;
                    bestMove    = nbr;
                }
            }

            if (bestMove != unit->position()) {
                aoc::sim::orderUnitMove(*unit, bestMove, grid);
                aoc::sim::moveUnitAlongPath(gameState, *unit, grid);
            }
            continue;
        }

        // ============================================================
        // MILITARY UNITS
        // ============================================================
        if (!aoc::sim::isMilitary(def.unitClass)) {
            continue;
        }

        // --- Priority 1: Ranged attack within range ---
        if (def.rangedStrength > 0 && def.range > 0) {
            const EnemyUnitSnapshot* bestTarget    = nullptr;
            int32_t                  bestTargetDist = std::numeric_limits<int32_t>::max();

            for (const EnemyUnitSnapshot& enemy : enemySnapshots) {
                const int32_t dist = aoc::hex::distance(unit->position(), enemy.position);
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
                        aoc::sim::resolveRangedCombat(gameState, rng, grid, *unit, *targetUnit);
                        // Re-look-up: our unit may have died from counter-fire.
                        unit = gsPlayer->unitAt(snap.position);
                        if (unit == nullptr) {
                            continue;
                        }
                    }
                }
            }
        }

        // --- Priority 1 (melee): Attack the weakest adjacent enemy ---
        bool attacked = false;
        if (def.rangedStrength == 0 || def.range == 0) {
            // Find the weakest adjacent enemy to maximise chance of a kill.
            const EnemyUnitSnapshot* weakestAdj  = nullptr;
            int32_t                  weakestStr   = std::numeric_limits<int32_t>::max();

            for (const aoc::hex::AxialCoord& nbr : neighborTiles) {
                if (!grid.isValid(nbr)) {
                    continue;
                }
                for (const EnemyUnitSnapshot& enemy : enemySnapshots) {
                    if (enemy.position != nbr) {
                        continue;
                    }
                    if (enemy.combatStrength < weakestStr) {
                        weakestStr  = enemy.combatStrength;
                        weakestAdj  = &enemy;
                    }
                }
            }

            if (weakestAdj != nullptr) {
                aoc::game::Player* enemyPlayer = gameState.player(weakestAdj->owner);
                if (enemyPlayer != nullptr) {
                    aoc::game::Unit* targetUnit = enemyPlayer->unitAt(weakestAdj->position);
                    if (targetUnit != nullptr) {
                        aoc::sim::resolveMeleeCombat(gameState, rng, grid, *unit, *targetUnit);
                        attacked = true;
                        // Re-look-up: we may have died in the exchange.
                        unit = gsPlayer->unitAt(snap.position);
                    }
                }
            }
        }

        if (attacked || unit == nullptr) {
            continue;
        }

        // --- Priority 2: Close in on an enemy within 3 tiles ---
        {
            const EnemyUnitSnapshot* nearestEnemy = nullptr;
            int32_t                  nearestDist   = std::numeric_limits<int32_t>::max();

            for (const EnemyUnitSnapshot& enemy : enemySnapshots) {
                const int32_t dist = aoc::hex::distance(unit->position(), enemy.position);
                if (dist <= 3 && dist < nearestDist) {
                    nearestDist  = dist;
                    nearestEnemy = &enemy;
                }
            }

            if (nearestEnemy != nullptr && nearestDist > 1) {
                // Step toward the enemy using the neighbour that minimises distance.
                aoc::hex::AxialCoord bestMove    = unit->position();
                int32_t              bestMoveDist = nearestDist;

                for (const aoc::hex::AxialCoord& nbr : neighborTiles) {
                    if (!grid.isValid(nbr) || grid.movementCost(grid.toIndex(nbr)) <= 0) {
                        continue;
                    }
                    const int32_t d = aoc::hex::distance(nbr, nearestEnemy->position);
                    if (d < bestMoveDist) {
                        bestMoveDist = d;
                        bestMove     = nbr;
                    }
                }

                if (bestMove != unit->position()) {
                    aoc::sim::orderUnitMove(*unit, bestMove, grid);
                    aoc::sim::moveUnitAlongPath(gameState, *unit, grid);
                }
                continue;
            }
        }

        // --- Priority 3: Defend a threatened city ---
        {
            bool                 cityThreatened    = false;
            aoc::hex::AxialCoord threatenedCityPos{};

            for (const aoc::hex::AxialCoord& cityLoc : ownCityLocs) {
                for (const EnemyUnitSnapshot& enemy : enemySnapshots) {
                    if (aoc::hex::distance(cityLoc, enemy.position) <= 3) {
                        cityThreatened    = true;
                        threatenedCityPos = cityLoc;
                        break;
                    }
                }
                if (cityThreatened) {
                    break;
                }
            }

            if (cityThreatened) {
                const int32_t distToCity = aoc::hex::distance(unit->position(), threatenedCityPos);
                if (distToCity > 1) {
                    aoc::sim::orderUnitMove(*unit, threatenedCityPos, grid);
                    aoc::sim::moveUnitAlongPath(gameState, *unit, grid);
                }
                continue;
            }
        }

        // --- Priority 4: Seek the nearest enemy unit or city ---
        {
            int32_t              closestDist   = std::numeric_limits<int32_t>::max();
            aoc::hex::AxialCoord closestTarget = unit->position();

            // Hard difficulty: prioritise enemy cities for offensive pressure.
            if (hardMode) {
                for (const EnemyCitySnapshot& ecity : enemyCities) {
                    const int32_t dist = aoc::hex::distance(unit->position(), ecity.position);
                    if (dist < closestDist) {
                        closestDist   = dist;
                        closestTarget = ecity.position;
                    }
                }
            }

            for (const EnemyUnitSnapshot& enemy : enemySnapshots) {
                const int32_t dist = aoc::hex::distance(unit->position(), enemy.position);
                if (dist < closestDist) {
                    closestDist   = dist;
                    closestTarget = enemy.position;
                }
            }

            if (!hardMode) {
                for (const EnemyCitySnapshot& ecity : enemyCities) {
                    const int32_t dist = aoc::hex::distance(unit->position(), ecity.position);
                    if (dist < closestDist) {
                        closestDist   = dist;
                        closestTarget = ecity.position;
                    }
                }
            }

            if (closestDist < std::numeric_limits<int32_t>::max() && closestDist > 1) {
                aoc::hex::AxialCoord bestMove    = unit->position();
                int32_t              bestMoveDist = closestDist;

                for (const aoc::hex::AxialCoord& nbr : neighborTiles) {
                    if (!grid.isValid(nbr) || grid.movementCost(grid.toIndex(nbr)) <= 0) {
                        continue;
                    }
                    const int32_t d = aoc::hex::distance(nbr, closestTarget);
                    if (d < bestMoveDist) {
                        bestMoveDist = d;
                        bestMove     = nbr;
                    }
                }

                if (bestMove != unit->position()) {
                    aoc::sim::orderUnitMove(*unit, bestMove, grid);
                    aoc::sim::moveUnitAlongPath(gameState, *unit, grid);
                }
                continue;
            }
        }

        // --- Priority 5: Patrol the nearest border tile ---
        {
            aoc::hex::AxialCoord bestBorder    = unit->position();
            int32_t              bestBorderScore = std::numeric_limits<int32_t>::min();

            std::vector<aoc::hex::AxialCoord> searchTiles;
            searchTiles.reserve(60);
            aoc::hex::spiral(unit->position(), 4, std::back_inserter(searchTiles));

            for (const aoc::hex::AxialCoord& tile : searchTiles) {
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

                const std::array<aoc::hex::AxialCoord, 6> tileNbrs = aoc::hex::neighbors(tile);
                int32_t unownedNeighbors = 0;
                for (const aoc::hex::AxialCoord& tn : tileNbrs) {
                    if (!grid.isValid(tn) || grid.owner(grid.toIndex(tn)) != this->m_player) {
                        ++unownedNeighbors;
                    }
                }
                if (unownedNeighbors == 0) {
                    continue;
                }

                int32_t borderScore = unownedNeighbors * 10;
                const aoc::map::FeatureType feat = grid.feature(tileIdx);
                if (feat == aoc::map::FeatureType::Hills)  { borderScore += 5; }
                if (feat == aoc::map::FeatureType::Forest) { borderScore += 3; }
                borderScore -= aoc::hex::distance(unit->position(), tile);

                if (borderScore > bestBorderScore) {
                    bestBorderScore = borderScore;
                    bestBorder      = tile;
                }
            }

            if (bestBorder != unit->position()) {
                aoc::sim::orderUnitMove(*unit, bestBorder, grid);
                aoc::sim::moveUnitAlongPath(gameState, *unit, grid);
            } else {
                // Already at the best border tile -- fortify on defensive terrain
                // or make a random patrol move to avoid idling in the open.
                const int32_t              unitIdx  = grid.toIndex(unit->position());
                const aoc::map::FeatureType unitFeat = grid.feature(unitIdx);

                if ((unitFeat == aoc::map::FeatureType::Hills ||
                     unitFeat == aoc::map::FeatureType::Forest) &&
                    unit->state() != aoc::sim::UnitState::Fortified)
                {
                    unit->setState(aoc::sim::UnitState::Fortified);
                    LOG_INFO("AI %u Unit at (%d,%d) fortified on defensive terrain",
                             static_cast<unsigned>(this->m_player),
                             unit->position().q, unit->position().r);
                } else {
                    // Random patrol step to avoid clustering.
                    std::vector<aoc::hex::AxialCoord> validMoves;
                    for (const aoc::hex::AxialCoord& nbr : neighborTiles) {
                        if (grid.isValid(nbr) && grid.movementCost(grid.toIndex(nbr)) > 0) {
                            validMoves.push_back(nbr);
                        }
                    }
                    if (!validMoves.empty()) {
                        const int32_t idx = rng.nextInt(
                            0, static_cast<int32_t>(validMoves.size()) - 1);
                        aoc::sim::orderUnitMove(*unit, validMoves[static_cast<std::size_t>(idx)], grid);
                        aoc::sim::moveUnitAlongPath(gameState, *unit, grid);
                    }
                }
            }
        }
    }

    // ----------------------------------------------------------------
    // Opportunistic aggression: when this player has a significant military
    // advantage AND the threat ratio is low (safe to attack), seek a weak
    // neighbour to declare war on.  This fires independently of the diplomacy
    // manager so the military controller can coordinate movement orders with
    // the declaration in the same turn.
    //
    // Conditions:
    //   - own military >= 4 units (enough to mount an actual assault)
    //   - threat ratio < 0.3 (no credible inbound threat)
    //   - a reachable neighbour has <= half our military strength
    // ----------------------------------------------------------------
    {
        const int32_t ownMilitary = gsPlayer->militaryUnitCount();
        if (ownMilitary >= 4 && threatRatio < 0.3f) {
            // Identify the weakest neighbour within striking range (any city within 15 tiles).
            PlayerId  weakestNeighbour = INVALID_PLAYER;
            int32_t   weakestMilitary  = ownMilitary / 2;  // must be <= half our strength

            for (const std::unique_ptr<aoc::game::Player>& other : gameState.players()) {
                if (other->id() == this->m_player) {
                    continue;
                }
                // Check if this player has a city within striking range.
                bool hasNearCity = false;
                for (const std::unique_ptr<aoc::game::City>& city : other->cities()) {
                    for (const aoc::hex::AxialCoord& ownCity : ownCityLocs) {
                        if (aoc::hex::distance(city->location(), ownCity) <= 15) {
                            hasNearCity = true;
                            break;
                        }
                    }
                    if (hasNearCity) { break; }
                }
                if (!hasNearCity) { continue; }

                const int32_t theirMilitary = other->militaryUnitCount();
                if (theirMilitary < weakestMilitary) {
                    weakestMilitary  = theirMilitary;
                    weakestNeighbour = other->id();
                }
            }

            if (weakestNeighbour != INVALID_PLAYER) {
                // Move every military unit toward the nearest enemy city.
                for (const OwnedUnitSnapshot& snap : ownedSnapshots) {
                    aoc::game::Unit* unit = gsPlayer->unitAt(snap.position);
                    if (unit == nullptr || unit->movementRemaining() <= 0) {
                        continue;
                    }
                    const aoc::sim::UnitTypeDef& def = aoc::sim::unitTypeDef(unit->typeId());
                    if (!aoc::sim::isMilitary(def.unitClass)) {
                        continue;
                    }

                    const aoc::game::Player* targetPlayer = gameState.player(weakestNeighbour);
                    if (targetPlayer == nullptr || targetPlayer->cities().empty()) {
                        continue;
                    }

                    // Find the nearest enemy city to this unit.
                    aoc::hex::AxialCoord targetCity = targetPlayer->cities().front()->location();
                    int32_t              bestDist   = aoc::hex::distance(unit->position(), targetCity);
                    for (const std::unique_ptr<aoc::game::City>& city : targetPlayer->cities()) {
                        const int32_t d = aoc::hex::distance(unit->position(), city->location());
                        if (d < bestDist) {
                            bestDist   = d;
                            targetCity = city->location();
                        }
                    }

                    if (bestDist > 1) {
                        // Step toward the target city using the neighbour closest to it.
                        const std::array<aoc::hex::AxialCoord, 6> unitNbrs =
                            aoc::hex::neighbors(unit->position());
                        aoc::hex::AxialCoord bestMove    = unit->position();
                        int32_t              bestMoveDist = bestDist;
                        for (const aoc::hex::AxialCoord& nbr : unitNbrs) {
                            if (!grid.isValid(nbr) || grid.movementCost(grid.toIndex(nbr)) <= 0) {
                                continue;
                            }
                            const int32_t d = aoc::hex::distance(nbr, targetCity);
                            if (d < bestMoveDist) {
                                bestMoveDist = d;
                                bestMove     = nbr;
                            }
                        }
                        if (bestMove != unit->position()) {
                            aoc::sim::orderUnitMove(*unit, bestMove, grid);
                            aoc::sim::moveUnitAlongPath(gameState, *unit, grid);
                        }
                    }
                }

                LOG_INFO("AI %u Military advantage: %d units vs neighbour %u with %d -- "
                         "converging on their cities",
                         static_cast<unsigned>(this->m_player),
                         ownMilitary,
                         static_cast<unsigned>(weakestNeighbour),
                         weakestMilitary);
            }
        }
    }

    // ----------------------------------------------------------------
    // Unit composition: warn/log if below era minimum.
    // The AIController layer is responsible for triggering production;
    // here we emit a structured log entry that the controller can read.
    // ----------------------------------------------------------------
    {
        const aoc::EraId currentEra = gsPlayer->era().currentEra;
        const int32_t    cityCount  = gsPlayer->cityCount();
        const int32_t    desired    = desiredMilitaryUnits(currentEra, cityCount);
        const int32_t    actual     = gsPlayer->militaryUnitCount();

        if (actual < desired) {
            LOG_INFO("AI %u Military below target: %d/%d (era=%u, cities=%d) -- rebuild needed",
                     static_cast<unsigned>(this->m_player),
                     actual, desired,
                     static_cast<unsigned>(currentEra.value),
                     cityCount);
        }
    }
}

} // namespace aoc::sim::ai
