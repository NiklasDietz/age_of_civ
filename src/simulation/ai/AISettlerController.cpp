/**
 * @file AISettlerController.cpp
 * @brief AI settler management: city location scoring, settler movement,
 *        and city founding with proper tile yield weighting and proximity bonuses.
 *
 * Scoring incorporates ring-1 and ring-2 tile yields, fresh-water adjacency,
 * coastal access, resource bonuses, and inter-city distance penalties.
 * Settlers store a pre-computed target location and pursue it directly.
 *
 * Identity tracking uses axial map coordinates rather than unit pointer
 * addresses.  This makes the stuck-turn counter survive settler replacement:
 * when a city produces a new settler at the same tile the new unit inherits
 * the previous counter and will force-found after just one more stuck turn.
 *
 * Use-after-free is prevented by snapshotting raw pointers before any
 * removeUnit() call.  All data needed for logging is captured in local
 * variables before removal.  The loop immediately continues after removal so
 * the dangling pointer is never touched again.
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

static constexpr uint16_t LUXURY_RESOURCE_ID_MIN    = 20;
static constexpr uint16_t LUXURY_RESOURCE_ID_MAX    = 34;
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

/**
 * @brief Returns true when a tile at @p pos is passable and available for
 *        city founding by @p player (not water, not impassable, not owned
 *        by a different player).
 */
[[nodiscard]] static bool isTileFoundable(aoc::hex::AxialCoord pos,
                                           const aoc::map::HexGrid& grid,
                                           PlayerId player) {
    if (!grid.isValid(pos)) {
        return false;
    }
    const int32_t idx = grid.toIndex(pos);
    if (aoc::map::isWater(grid.terrain(idx)) || aoc::map::isImpassable(grid.terrain(idx))) {
        return false;
    }
    const PlayerId owner = grid.owner(idx);
    return owner == INVALID_PLAYER || owner == player;
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
 *      and store it in m_settlerTargets keyed by the settler's current tile.
 *   2. Each subsequent turn, move toward the stored target.
 *   3. When the settler arrives at the target, found the city immediately.
 *   4. If the target was occupied by an enemy, recompute.
 *   5. If stuck for 1 turn without movement, found at the current tile if
 *      passable, or at the best adjacent passable tile otherwise.
 *
 * Tracking key: axial tile coordinate of the settler.  A fresh settler
 * produced at the same city tile inherits the previous settler's stuck count,
 * ensuring it force-founds on the very next stuck turn rather than resetting
 * to zero and waiting indefinitely.
 *
 * Use-after-free safety: all data needed for logging is captured before
 * removeUnit() is called.  The loop immediately continues after removal.
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
    // pointer becomes dangling.  We only hold raw pointers in our snapshot and
    // never dereference a pointer after removeUnit() is called on it.
    struct SettlerSnapshot {
        aoc::game::Unit*     ptr;
        aoc::hex::AxialCoord position;
        int32_t              movementRemaining;
    };

    std::vector<SettlerSnapshot> settlers;
    settlers.reserve(8);
    for (const std::unique_ptr<aoc::game::Unit>& unitPtr : gsPlayer->units()) {
        if (aoc::sim::unitTypeDef(unitPtr->typeId()).unitClass == aoc::sim::UnitClass::Settler) {
            settlers.push_back({unitPtr.get(), unitPtr->position(), unitPtr->movementRemaining()});
        }
    }

    // A settler stuck at the same position for 1 turn is force-founded.
    // Reducing from 2 to 1 ensures a settler produced at a city tile (which
    // has 0 movement that turn) founds immediately on its next turn rather
    // than waiting a full extra turn.
    constexpr int32_t STUCK_TURNS_LIMIT = 1;
    // Search radius for best city location.
    constexpr int32_t SEARCH_RADIUS     = 15;
    // Minimum score for a tile to be considered a valid founding site.
    constexpr float   FOUND_SCORE_MIN   = -500.0f;
    // If the target is farther than this and the settler has no movement,
    // found at the current tile to avoid the unit sitting idle forever.
    constexpr int32_t FAR_TARGET_DIST   = 5;

    for (const SettlerSnapshot& snap : settlers) {
        // Safety check: unit may have been killed by events between snapshot and loop.
        if (snap.ptr->isDead()) {
            this->m_settlerStuckTurns.erase(snap.position);
            this->m_settlerTargets.erase(snap.position);
            continue;
        }

        // Update stuck-turn counter using the tile coordinate as key.
        // A new settler produced at a tile where the previous one was stuck
        // inherits the accumulated count, keeping pressure on expansion.
        int32_t& stuckTurns = this->m_settlerStuckTurns[snap.position];
        // The counter increments every time a settler is seen at this tile
        // without having moved.  It is reset to 0 when the settler moves away.
        ++stuckTurns;

        // If the settler moved here from another tile, clear the old entry.
        // (New entries are default-initialised to 0 by operator[], so the
        //  first increment produces 1 -- exactly one stuck turn.)

        // --- Validate or (re)compute target ---
        bool needsNewTarget = (this->m_settlerTargets.count(snap.position) == 0);

        if (!needsNewTarget) {
            const aoc::hex::AxialCoord storedTarget = this->m_settlerTargets.at(snap.position);
            if (grid.isValid(storedTarget)) {
                const int32_t targetIdx = grid.toIndex(storedTarget);
                const PlayerId targetOwner = grid.owner(targetIdx);
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
            float bestScore = std::numeric_limits<float>::lowest();
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

            if (bestScore <= -9999.0f) {
                // Every candidate was disqualified.  Found at current tile if
                // habitable, otherwise skip this turn.
                if (isTileFoundable(snap.position, grid, this->m_player)) {
                    const aoc::hex::AxialCoord foundPos = snap.position;
                    const std::string          cityName = getNextCityName(gameState, this->m_player);

                    this->m_settlerStuckTurns.erase(snap.position);
                    this->m_settlerTargets.erase(snap.position);

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
                LOG_WARN("AI %u [AISettlerController.cpp:executeSettlerActions] "
                         "settler at (%d,%d) has no valid founding site -- waiting",
                         static_cast<unsigned>(this->m_player),
                         snap.position.q, snap.position.r);
                continue;
            }

            this->m_settlerTargets[snap.position] = bestLocation;

            LOG_INFO("AI %u Settler at (%d,%d) targeting (%d,%d) score=%.1f",
                     static_cast<unsigned>(this->m_player),
                     snap.position.q, snap.position.r,
                     bestLocation.q, bestLocation.r,
                     static_cast<double>(bestScore));
        }

        const aoc::hex::AxialCoord target = this->m_settlerTargets.at(snap.position);

        // --- Found immediately when settler has arrived at the target ---
        if (snap.position == target) {
            const float arrivalScore = scoreCityLocation(
                snap.position, grid, gameState, this->m_player);

            if (arrivalScore > FOUND_SCORE_MIN) {
                const aoc::hex::AxialCoord foundPos = snap.position;
                const std::string          cityName = getNextCityName(gameState, this->m_player);

                this->m_settlerStuckTurns.erase(snap.position);
                this->m_settlerTargets.erase(snap.position);

                foundCity(gameState, grid, this->m_player, foundPos, cityName);
                gsPlayer->removeUnit(snap.ptr);

                LOG_INFO("AI %u Founded '%s' at (%d,%d) score=%.1f",
                         static_cast<unsigned>(this->m_player),
                         cityName.c_str(),
                         foundPos.q, foundPos.r,
                         static_cast<double>(arrivalScore));
                continue;
            }

            // Score below threshold at the computed target -- discard and recompute.
            LOG_INFO("AI %u Target (%d,%d) score=%.1f below threshold -- discarding target",
                     static_cast<unsigned>(this->m_player),
                     snap.position.q, snap.position.r,
                     static_cast<double>(arrivalScore));
            this->m_settlerTargets.erase(snap.position);
            continue;
        }

        // --- Immediately found if no movement and target is far away ---
        // A settler produced this turn has 0 movement points.  If the ideal
        // target is beyond FAR_TARGET_DIST tiles the settler will never reach
        // it this turn; founding at the current tile (near the capital) is
        // better than waiting until the stuck limit.
        if (snap.movementRemaining == 0
            && aoc::hex::distance(snap.position, target) > FAR_TARGET_DIST) {
            if (isTileFoundable(snap.position, grid, this->m_player)) {
                const aoc::hex::AxialCoord foundPos = snap.position;
                const std::string          cityName = getNextCityName(gameState, this->m_player);

                this->m_settlerStuckTurns.erase(snap.position);
                this->m_settlerTargets.erase(snap.position);

                foundCity(gameState, grid, this->m_player, foundPos, cityName);
                gsPlayer->removeUnit(snap.ptr);

                LOG_INFO("AI %u Founded '%s' at (%d,%d) -- no movement and target > %d tiles",
                         static_cast<unsigned>(this->m_player),
                         cityName.c_str(),
                         foundPos.q, foundPos.r,
                         FAR_TARGET_DIST);
                continue;
            }
        }

        // --- Force-found if stuck for STUCK_TURNS_LIMIT turns ---
        if (stuckTurns >= STUCK_TURNS_LIMIT) {
            // Prefer the current tile; if it is occupied by a city, search the
            // 6 adjacent tiles for the best passable alternative.
            aoc::hex::AxialCoord foundPos = snap.position;
            bool foundPassable = isTileFoundable(snap.position, grid, this->m_player);

            if (!foundPassable) {
                // Current tile is a city or impassable -- check neighbours.
                float bestAdjacentScore = std::numeric_limits<float>::lowest();
                const std::array<aoc::hex::AxialCoord, 6> nbrs = aoc::hex::neighbors(snap.position);
                for (const aoc::hex::AxialCoord& nbr : nbrs) {
                    if (!isTileFoundable(nbr, grid, this->m_player)) {
                        continue;
                    }
                    const float nbrScore = scoreCityLocation(nbr, grid, gameState, this->m_player);
                    if (nbrScore > bestAdjacentScore) {
                        bestAdjacentScore = nbrScore;
                        foundPos          = nbr;
                        foundPassable     = true;
                    }
                }
            }

            if (foundPassable) {
                const int32_t     logStuck = stuckTurns;
                const std::string cityName = getNextCityName(gameState, this->m_player);

                // Erase both the old position entry and the new one (they may differ).
                this->m_settlerStuckTurns.erase(snap.position);
                this->m_settlerTargets.erase(snap.position);

                foundCity(gameState, grid, this->m_player, foundPos, cityName);
                gsPlayer->removeUnit(snap.ptr);

                LOG_INFO("AI %u Force-founded '%s' at (%d,%d) after %d stuck turn(s)",
                         static_cast<unsigned>(this->m_player),
                         cityName.c_str(),
                         foundPos.q, foundPos.r,
                         logStuck);
                continue;
            }

            // Neither current tile nor any neighbour is passable -- discard
            // target and try again next turn with a fresh search.
            this->m_settlerTargets.erase(snap.position);
            continue;
        }

        // --- Move toward the target ---
        if (snap.movementRemaining > 0) {
            aoc::sim::orderUnitMove(*snap.ptr, target, grid);
            aoc::sim::moveUnitAlongPath(gameState, *snap.ptr, grid);

            // If the settler moved, remove the old position entry so the new
            // position starts with a clean stuck count.
            const aoc::hex::AxialCoord newPos = snap.ptr->position();
            if (newPos != snap.position) {
                this->m_settlerStuckTurns.erase(snap.position);
                this->m_settlerTargets.erase(snap.position);
                // The new position entry will be created on the next call when
                // this settler is processed from its new tile.
            }
        }
    }
}

} // namespace aoc::sim::ai
