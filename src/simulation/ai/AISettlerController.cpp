/**
 * @file AISettlerController.cpp
 * @brief AI settler management: city location scoring, settler movement,
 *        and city founding with proper tile yield weighting and proximity bonuses.
 *
 * Scoring incorporates ring-1 and ring-2 tile yields, fresh-water adjacency,
 * coastal access, resource bonuses, and inter-city distance penalties.
 * Settlers store a pre-computed target location and pursue it directly.
 * Use-after-free is prevented by snapshotting pointer identity before any
 * removeUnit() call and immediately continuing after removal.
 */

#include "aoc/simulation/ai/AISettlerController.hpp"

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/unit/Movement.hpp"
#include "aoc/simulation/turn/TurnProcessor.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"

#include <limits>
#include <vector>

namespace aoc::sim::ai {

// ============================================================================
// Luxury resource ID range: IDs 20-34 are luxury goods.
// Strategic resource IDs: 0-12.
// ============================================================================

static constexpr uint16_t LUXURY_RESOURCE_ID_MIN  = 20;
static constexpr uint16_t LUXURY_RESOURCE_ID_MAX  = 34;
static constexpr uint16_t STRATEGIC_RESOURCE_ID_MAX = 12;

/// Returns true when the resource ID belongs to the luxury category.
[[nodiscard]] static bool isLuxuryResource(aoc::ResourceId res) {
    return res.value >= LUXURY_RESOURCE_ID_MIN && res.value <= LUXURY_RESOURCE_ID_MAX;
}

/// Returns true when the resource ID belongs to the strategic category.
[[nodiscard]] static bool isStrategicResource(aoc::ResourceId res) {
    return res.isValid() && res.value <= STRATEGIC_RESOURCE_ID_MAX;
}

// ============================================================================
// scoreCityLocation -- full weighted formula using ring-1 + ring-2 tiles.
// ============================================================================

/**
 * @brief Compute a placement score for founding a city at @p pos.
 *
 * Higher is better. Returns -9999.0f for tiles that can never host a city
 * (water, mountain, already owned by a third party).
 *
 * Formula:
 *   Yield scan (ring 1 + ring 2): food*3 + production*2 + gold*1 + science*1.5
 *   Bonuses: fresh-water (+10), coastal access (+6), luxury per tile (+8),
 *            strategic per tile (+5)
 *   Penalties: own city < 3 tiles (-40), own city > 10 tiles (-3/excess tile),
 *              enemy city < 4 tiles (-20), tile owned by enemy (-30)
 *
 * @param pos        Candidate location in axial coordinates.
 * @param grid       Hex grid for terrain and resource queries.
 * @param gameState  Full game state for city-distance penalties.
 * @param player     The AI player evaluating this location.
 * @return Placement score. Negative values are below the founding threshold.
 */
[[nodiscard]] static float scoreCityLocation(aoc::hex::AxialCoord pos,
                                              const aoc::map::HexGrid& grid,
                                              const aoc::game::GameState& gameState,
                                              PlayerId player) {
    if (!grid.isValid(pos)) {
        return -9999.0f;
    }

    const int32_t centerIdx = grid.toIndex(pos);
    const aoc::map::TerrainType centerTerrain = grid.terrain(centerIdx);

    // Hard disqualification: water and mountains cannot host cities.
    if (aoc::map::isWater(centerTerrain) || aoc::map::isImpassable(centerTerrain)) {
        return -9999.0f;
    }

    // Tile owned by another player cannot be settled.
    const PlayerId tileOwner = grid.owner(centerIdx);
    if (tileOwner != INVALID_PLAYER && tileOwner != player) {
        return -9999.0f;
    }

    float score = 0.0f;

    // Scan ring-1 and ring-2 tiles for yields and resource bonuses.
    bool hasFreshWater = false;
    int32_t coastCount = 0;

    // Collect ring-1 (distance 1) then ring-2 (distance 2) tiles.
    std::vector<aoc::hex::AxialCoord> scanTiles;
    scanTiles.reserve(18); // 6 + 12
    aoc::hex::ring(pos, 1, std::back_inserter(scanTiles));
    aoc::hex::ring(pos, 2, std::back_inserter(scanTiles));

    for (const aoc::hex::AxialCoord& tile : scanTiles) {
        if (!grid.isValid(tile)) {
            continue;
        }

        const int32_t tileIdx = grid.toIndex(tile);
        const aoc::map::TerrainType tileTerrain = grid.terrain(tileIdx);
        const aoc::map::TileYield yield = grid.tileYield(tileIdx);

        score += static_cast<float>(yield.food)       * 3.0f;
        score += static_cast<float>(yield.production)  * 2.0f;
        score += static_cast<float>(yield.gold)        * 1.0f;
        score += static_cast<float>(yield.science)     * 1.5f;

        // Coast adjacency: up to 3 coastal tiles qualify for harbor bonus.
        if (aoc::map::isWater(tileTerrain)) {
            ++coastCount;
        }

        // River adjacency provides fresh water (housing + city growth).
        if (grid.riverEdges(tileIdx) != 0) {
            hasFreshWater = true;
        }

        // Resource bonuses within the workable radius.
        const aoc::ResourceId res = grid.resource(tileIdx);
        if (res.isValid()) {
            if (isLuxuryResource(res)) {
                score += 8.0f;
            } else if (isStrategicResource(res)) {
                score += 5.0f;
            }
        }
    }

    // Also check center tile river edges for fresh water.
    if (grid.riverEdges(centerIdx) != 0) {
        hasFreshWater = true;
    }

    if (hasFreshWater) {
        score += 10.0f;
    }

    // Coastal access: 1-3 adjacent water tiles indicate harbor potential.
    if (coastCount > 0 && coastCount <= 3) {
        score += 6.0f;
    }

    // City distance penalties and bonuses.
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        for (const std::unique_ptr<aoc::game::City>& city : playerPtr->cities()) {
            const int32_t dist = aoc::hex::distance(pos, city->location());

            if (city->owner() == player) {
                // Too close to own city: tiles overlap and culture areas overlap.
                if (dist < 3) {
                    score -= 40.0f;
                } else if (dist > 10) {
                    // Too far from own empire: hard to connect and defend.
                    score -= static_cast<float>(dist - 10) * 3.0f;
                }
            } else {
                // Dangerous forward settle: risk of early war.
                if (dist < 4) {
                    score -= 20.0f;
                }
            }
        }
    }

    return score;
}

// ============================================================================
// Constructor
// ============================================================================

AISettlerController::AISettlerController(PlayerId player, aoc::ui::AIDifficulty difficulty)
    : m_player(player)
    , m_difficulty(difficulty)
{
}

// ============================================================================
// executeSettlerActions
// ============================================================================

/**
 * @brief Process all settler units for this AI player for one turn.
 *
 * Behaviour per settler:
 *   1. On first encounter, compute the best city location within radius 15
 *      and store it in m_settlerTargets.
 *   2. Each subsequent turn, move toward the stored target.
 *   3. When the settler arrives at the target, found the city.
 *   4. If the target became invalid (enemy settled there), recompute.
 *   5. If stuck for 3+ turns, found at current position if the tile is passable.
 *
 * Use-after-free safety: the settlers vector is a snapshot of pointer addresses
 * taken before any modification. All data needed for logging is captured in local
 * variables before removeUnit() is called. The loop immediately continues after
 * removal so the dangling pointer is never touched again.
 */
void AISettlerController::executeSettlerActions(aoc::game::GameState& gameState,
                                                aoc::map::HexGrid& grid) {
    aoc::game::Player* gsPlayer = gameState.player(this->m_player);
    if (gsPlayer == nullptr) {
        return;
    }

    LOG_INFO("AI %u executeSettlerActions: %d cities, %d units",
             static_cast<unsigned>(this->m_player),
             gsPlayer->cityCount(),
             gsPlayer->unitCount());

    // --- Snapshot settler units before iteration ---
    // After removeUnit() the unique_ptr inside m_units is destroyed; the raw
    // pointer becomes dangling. We only hold raw pointers in our snapshot and
    // we never dereference a pointer after removeUnit() is called on it.
    struct SettlerSnapshot {
        aoc::game::Unit*     ptr;
        aoc::hex::AxialCoord position;
        int32_t              movementRemaining;
        uintptr_t            key;   ///< Stable identity key (pointer address at snapshot time)
    };

    std::vector<SettlerSnapshot> settlers;
    settlers.reserve(8);
    for (const std::unique_ptr<aoc::game::Unit>& unitPtr : gsPlayer->units()) {
        if (aoc::sim::unitTypeDef(unitPtr->typeId()).unitClass == aoc::sim::UnitClass::Settler) {
            const uintptr_t key = reinterpret_cast<uintptr_t>(unitPtr.get());
            settlers.push_back({unitPtr.get(), unitPtr->position(), unitPtr->movementRemaining(), key});
        }
    }

    // Number of turns without movement before force-founding.
    constexpr int32_t STUCK_TURNS_LIMIT  = 2;
    // Search radius for best city location.
    constexpr int32_t SEARCH_RADIUS      = 15;
    // Minimum score for a tile to be considered a valid founding site.
    constexpr float   FOUND_SCORE_MIN    = -500.0f;

    for (const SettlerSnapshot& snap : settlers) {
        // Safety check: unit may have been killed by events between snapshot and loop.
        if (snap.ptr->isDead()) {
            this->m_settlerStuckTurns.erase(snap.key);
            this->m_settlerLastPosition.erase(snap.key);
            this->m_settlerTargets.erase(snap.key);
            continue;
        }

        // Update stuck-turn counter.
        int32_t& stuckTurns = this->m_settlerStuckTurns[snap.key];
        aoc::hex::AxialCoord& lastPos = this->m_settlerLastPosition[snap.key];

        if (lastPos == snap.position) {
            ++stuckTurns;
        } else {
            stuckTurns = 0;
        }
        lastPos = snap.position;

        // --- Validate or (re)compute target ---
        bool needsNewTarget = (this->m_settlerTargets.count(snap.key) == 0);

        if (!needsNewTarget) {
            // Recompute if the stored target is now occupied by an enemy city.
            const aoc::hex::AxialCoord storedTarget = this->m_settlerTargets.at(snap.key);
            if (grid.isValid(storedTarget)) {
                const int32_t targetIdx = grid.toIndex(storedTarget);
                const PlayerId targetOwner = grid.owner(targetIdx);
                // If another player already settled there, find a new target.
                if (targetOwner != INVALID_PLAYER && targetOwner != this->m_player) {
                    needsNewTarget = true;
                    LOG_INFO("AI %u Settler target at (%d,%d) was taken by player %u -- recomputing",
                             static_cast<unsigned>(this->m_player),
                             storedTarget.q, storedTarget.r,
                             static_cast<unsigned>(targetOwner));
                }
            } else {
                needsNewTarget = true;
            }
        }

        if (needsNewTarget) {
            float bestScore  = std::numeric_limits<float>::lowest();
            aoc::hex::AxialCoord bestLocation = snap.position;

            std::vector<aoc::hex::AxialCoord> candidates;
            candidates.reserve(750);
            aoc::hex::spiral(snap.position, SEARCH_RADIUS, std::back_inserter(candidates));

            for (const aoc::hex::AxialCoord& candidate : candidates) {
                if (!grid.isValid(candidate)) {
                    continue;
                }
                const float locationScore = scoreCityLocation(
                    candidate, grid, gameState, this->m_player);
                if (locationScore > bestScore) {
                    bestScore    = locationScore;
                    bestLocation = candidate;
                }
            }

            // Strong fallback: if the search found nothing better than -9999
            // (all tiles disqualified), found at the current position immediately
            // as long as it is habitable.  This prevents settlers from sitting
            // idle forever on edge-case maps (all-water surroundings, etc.).
            if (bestScore <= -9999.0f) {
                const int32_t curIdx = grid.toIndex(snap.position);
                const bool habitableHere = grid.isValid(snap.position)
                    && !aoc::map::isWater(grid.terrain(curIdx))
                    && !aoc::map::isImpassable(grid.terrain(curIdx))
                    && (grid.owner(curIdx) == INVALID_PLAYER
                        || grid.owner(curIdx) == this->m_player);

                if (habitableHere) {
                    const aoc::hex::AxialCoord foundPos = snap.position;
                    const std::string          cityName = getNextCityName(gameState, this->m_player);

                    this->m_settlerStuckTurns.erase(snap.key);
                    this->m_settlerLastPosition.erase(snap.key);
                    this->m_settlerTargets.erase(snap.key);

                    foundCity(gameState, grid, this->m_player, foundPos, cityName);
                    gsPlayer->removeUnit(snap.ptr);

                    LOG_WARN("AI %u [AISettlerController.cpp:executeSettlerActions] "
                             "no valid city location found in radius %d -- "
                             "founded '%s' at current position (%d,%d)",
                             static_cast<unsigned>(this->m_player),
                             SEARCH_RADIUS,
                             cityName.c_str(),
                             foundPos.q, foundPos.r);
                    continue;
                }
                // If even current position is bad, discard and wait.
                LOG_WARN("AI %u [AISettlerController.cpp:executeSettlerActions] "
                         "settler at (%d,%d) has no valid founding site -- waiting",
                         static_cast<unsigned>(this->m_player),
                         snap.position.q, snap.position.r);
                continue;
            }

            this->m_settlerTargets[snap.key] = bestLocation;

            LOG_INFO("AI %u Settler at (%d,%d) targeting (%d,%d) score=%.1f",
                     static_cast<unsigned>(this->m_player),
                     snap.position.q, snap.position.r,
                     bestLocation.q, bestLocation.r,
                     static_cast<double>(bestScore));
        }

        const aoc::hex::AxialCoord target = this->m_settlerTargets.at(snap.key);

        // --- Force-found if stuck too long ---
        if (stuckTurns >= STUCK_TURNS_LIMIT) {
            const int32_t centerIdx = grid.toIndex(snap.position);
            const bool passable = grid.isValid(snap.position)
                && !aoc::map::isWater(grid.terrain(centerIdx))
                && !aoc::map::isImpassable(grid.terrain(centerIdx))
                && (grid.owner(centerIdx) == INVALID_PLAYER
                    || grid.owner(centerIdx) == this->m_player);

            if (passable) {
                // Capture all logging data BEFORE removing the unit.
                const aoc::hex::AxialCoord foundPos  = snap.position;
                const int32_t              logStuck  = stuckTurns;
                const std::string          cityName  = getNextCityName(gameState, this->m_player);

                this->m_settlerStuckTurns.erase(snap.key);
                this->m_settlerLastPosition.erase(snap.key);
                this->m_settlerTargets.erase(snap.key);

                foundCity(gameState, grid, this->m_player, foundPos, cityName);
                gsPlayer->removeUnit(snap.ptr);
                // snap.ptr is now dangling -- do not access it again.

                LOG_INFO("AI %u Force-founded '%s' at (%d,%d) after being stuck %d turns",
                         static_cast<unsigned>(this->m_player),
                         cityName.c_str(),
                         foundPos.q, foundPos.r,
                         logStuck);
                continue;
            }
            // Cannot found here (impassable); try a different target next turn.
            this->m_settlerTargets.erase(snap.key);
            continue;
        }

        // --- Found city when we have arrived at the target ---
        if (snap.position == target) {
            const float arrivalScore = scoreCityLocation(
                snap.position, grid, gameState, this->m_player);

            if (arrivalScore > FOUND_SCORE_MIN) {
                // Capture all logging data BEFORE removing the unit.
                const aoc::hex::AxialCoord foundPos = snap.position;
                const std::string          cityName = getNextCityName(gameState, this->m_player);

                this->m_settlerStuckTurns.erase(snap.key);
                this->m_settlerLastPosition.erase(snap.key);
                this->m_settlerTargets.erase(snap.key);

                foundCity(gameState, grid, this->m_player, foundPos, cityName);
                gsPlayer->removeUnit(snap.ptr);
                // snap.ptr is now dangling -- do not access it again.

                LOG_INFO("AI %u Founded '%s' at (%d,%d) score=%.1f",
                         static_cast<unsigned>(this->m_player),
                         cityName.c_str(),
                         foundPos.q, foundPos.r,
                         static_cast<double>(arrivalScore));
                continue;
            }

            // Score is too low at the computed target. Discard and recompute next turn.
            LOG_INFO("AI %u Target (%d,%d) score=%.1f below threshold -- discarding target",
                     static_cast<unsigned>(this->m_player),
                     snap.position.q, snap.position.r,
                     static_cast<double>(arrivalScore));
            this->m_settlerTargets.erase(snap.key);
            continue;
        }

        // --- Move toward the target ---
        if (snap.movementRemaining > 0) {
            aoc::sim::orderUnitMove(*snap.ptr, target, grid);
            aoc::sim::moveUnitAlongPath(gameState, *snap.ptr, grid);
        }
    }
}

} // namespace aoc::sim::ai
