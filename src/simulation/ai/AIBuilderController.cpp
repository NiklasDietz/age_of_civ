/**
 * @file AIBuilderController.cpp
 * @brief AI builder management: tile improvements and resource prospecting.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/ai/AIBuilderController.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/simulation/unit/Movement.hpp"
#include "aoc/simulation/map/Improvement.hpp"
#include "aoc/simulation/event/VisibilityEvents.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"

#include <limits>
#include <unordered_set>
#include <vector>

namespace aoc::sim::ai {

// ============================================================================
// Constructor
// ============================================================================

AIBuilderController::AIBuilderController(PlayerId player, aoc::ui::AIDifficulty difficulty)
    : m_player(player)
    , m_difficulty(difficulty)
{
}

// ============================================================================
// Builder management
// ============================================================================

void AIBuilderController::manageBuildersAndImprovements(aoc::game::GameState& gameState,
                                                         aoc::map::HexGrid& grid) {
    aoc::game::Player* gsPlayer = gameState.player(this->m_player);
    if (gsPlayer == nullptr) {
        return;
    }

    // Collect owned city locations for proximity checks
    std::vector<aoc::hex::AxialCoord> cityLocations;
    for (const std::unique_ptr<aoc::game::City>& city : gsPlayer->cities()) {
        cityLocations.push_back(city->location());
    }

    if (cityLocations.empty()) {
        return;
    }

    // Snapshot civilian units (builders) with charges remaining
    struct BuilderSnapshot {
        aoc::game::Unit*     ptr;
        aoc::hex::AxialCoord position;
    };
    // Only actual Builder units (UnitTypeId{5}) -- other Civilian-class units
    // (Medic, Diplomat, Spy) share the class but must not improve tiles.
    std::vector<BuilderSnapshot> builders;
    for (const std::unique_ptr<aoc::game::Unit>& u : gsPlayer->units()) {
        if (u->typeId().value == 5 && u->chargesRemaining() != 0) {
            builders.push_back({u.get(), u->position()});
        }
    }

    // Tech bonus for prospecting: check if the player has researched TechId{10}
    const float prospectTechBonus = gsPlayer->hasResearched(TechId{10}) ? 0.15f : 0.0f;

    // Track tiles already targeted so multiple builders don't converge on the same tile
    std::unordered_set<aoc::hex::AxialCoord> targetedTiles;

    for (const BuilderSnapshot& builder : builders) {
        if (builder.ptr->isDead()) {
            continue;
        }
        if (builder.ptr->movementRemaining() <= 0) {
            continue;
        }

        // Step 1: Check if current tile can be improved
        const int32_t currentIdx = grid.toIndex(builder.position);
        if (grid.owner(currentIdx) == this->m_player &&
            grid.improvement(currentIdx) == aoc::map::ImprovementType::None &&
            grid.movementCost(currentIdx) > 0) {
            const aoc::map::ImprovementType bestImpr = bestImprovementForTile(grid, currentIdx);
            if (bestImpr != aoc::map::ImprovementType::None &&
                canPlaceImprovement(grid, currentIdx, bestImpr)) {
                grid.setImprovement(currentIdx, bestImpr);
                builder.ptr->useCharge();
                if (!builder.ptr->hasCharges()) {
                    gsPlayer->removeUnit(builder.ptr);
                    LOG_INFO("AI %u Builder exhausted after improving (%d,%d)",
                             static_cast<unsigned>(this->m_player),
                             builder.position.q, builder.position.r);
                    continue;
                }
                LOG_INFO("AI %u Builder improved tile (%d,%d)",
                         static_cast<unsigned>(this->m_player),
                         builder.position.q, builder.position.r);
                continue;
            }
        }

        // Step 1b: If standing on an owned land tile next to an unmined mountain
        // metal deposit, build a Mountain Mine there. The mountain itself is
        // impassable, so the builder stays where it is; the improvement is
        // applied to the adjacent mountain tile.
        if (grid.owner(currentIdx) == this->m_player &&
            grid.movementCost(currentIdx) > 0) {
            const std::array<aoc::hex::AxialCoord, 6> mountainNbrs =
                aoc::hex::neighbors(builder.position);
            bool builtMountainMine = false;
            for (const aoc::hex::AxialCoord& nbr : mountainNbrs) {
                if (!grid.isValid(nbr)) { continue; }
                const int32_t nbrIdx = grid.toIndex(nbr);
                if (grid.terrain(nbrIdx) != aoc::map::TerrainType::Mountain) { continue; }
                if (grid.improvement(nbrIdx) != aoc::map::ImprovementType::None) { continue; }
                if (!canPlaceImprovement(grid, nbrIdx, aoc::map::ImprovementType::MountainMine)) {
                    continue;
                }
                grid.setImprovement(nbrIdx, aoc::map::ImprovementType::MountainMine);
                // Claim the mountain tile for the player so the city can work it.
                if (grid.owner(nbrIdx) == INVALID_PLAYER) {
                    grid.setOwner(nbrIdx, this->m_player);
                }
                builder.ptr->useCharge();
                LOG_INFO("AI %u Builder built Mountain Mine on adjacent mountain (%d,%d)",
                         static_cast<unsigned>(this->m_player),
                         nbr.q, nbr.r);
                builtMountainMine = true;
                if (!builder.ptr->hasCharges()) {
                    gsPlayer->removeUnit(builder.ptr);
                }
                break;
            }
            if (builtMountainMine) {
                continue;
            }
        }

        // Step 2: Find nearest unimproved owned tile near any city
        aoc::hex::AxialCoord bestTarget = builder.position;
        int32_t bestDist = std::numeric_limits<int32_t>::max();

        // Step 2a: Priority hunt for owned land tiles adjacent to an unmined
        // mountain metal deposit. Mountain mines are high-value but unreachable
        // via the generic unimproved-tile search (mountains are impassable).
        // Widened to rings 1-6 since metal deposits are sparse and borders grow
        // over time.
        for (const aoc::hex::AxialCoord& cityLoc : cityLocations) {
            std::vector<aoc::hex::AxialCoord> searchTiles;
            searchTiles.reserve(120);
            aoc::hex::ring(cityLoc, 1, std::back_inserter(searchTiles));
            aoc::hex::ring(cityLoc, 2, std::back_inserter(searchTiles));
            aoc::hex::ring(cityLoc, 3, std::back_inserter(searchTiles));
            aoc::hex::ring(cityLoc, 4, std::back_inserter(searchTiles));
            aoc::hex::ring(cityLoc, 5, std::back_inserter(searchTiles));
            aoc::hex::ring(cityLoc, 6, std::back_inserter(searchTiles));

            for (const aoc::hex::AxialCoord& tile : searchTiles) {
                if (!grid.isValid(tile)) { continue; }
                const int32_t tIdx = grid.toIndex(tile);
                if (grid.owner(tIdx) != this->m_player) { continue; }
                if (grid.movementCost(tIdx) <= 0) { continue; }
                if (targetedTiles.find(tile) != targetedTiles.end()) { continue; }

                // Check if this owned land tile is adjacent to a mountain
                // where a Mountain Mine can be placed.
                const std::array<aoc::hex::AxialCoord, 6> nbrs = aoc::hex::neighbors(tile);
                bool hasMountainMineOpportunity = false;
                for (const aoc::hex::AxialCoord& nbr : nbrs) {
                    if (!grid.isValid(nbr)) { continue; }
                    const int32_t nbrIdx = grid.toIndex(nbr);
                    if (grid.terrain(nbrIdx) != aoc::map::TerrainType::Mountain) { continue; }
                    if (grid.improvement(nbrIdx) != aoc::map::ImprovementType::None) { continue; }
                    if (canPlaceImprovement(grid, nbrIdx, aoc::map::ImprovementType::MountainMine)) {
                        hasMountainMineOpportunity = true;
                        break;
                    }
                }
                if (!hasMountainMineOpportunity) { continue; }

                const int32_t dist = grid.distance(builder.position, tile);
                // Weight: prioritize mountain-mine tiles heavily (bias -3 hexes).
                const int32_t weighted = dist - 3;
                if (weighted < bestDist) {
                    bestDist = weighted;
                    bestTarget = tile;
                }
            }
        }

        for (const aoc::hex::AxialCoord& cityLoc : cityLocations) {
            std::vector<aoc::hex::AxialCoord> cityTiles;
            cityTiles.reserve(18);
            aoc::hex::ring(cityLoc, 1, std::back_inserter(cityTiles));
            aoc::hex::ring(cityLoc, 2, std::back_inserter(cityTiles));

            for (const aoc::hex::AxialCoord& tile : cityTiles) {
                if (!grid.isValid(tile)) {
                    continue;
                }
                const int32_t tileIdx = grid.toIndex(tile);
                if (grid.owner(tileIdx) != this->m_player) {
                    continue;
                }
                if (grid.improvement(tileIdx) != aoc::map::ImprovementType::None) {
                    continue;
                }
                if (grid.movementCost(tileIdx) <= 0) {
                    continue;
                }
                if (bestImprovementForTile(grid, tileIdx) == aoc::map::ImprovementType::None) {
                    continue;
                }
                if (targetedTiles.find(tile) != targetedTiles.end()) {
                    continue;
                }

                const int32_t dist = grid.distance(builder.position, tile);
                if (dist < bestDist) {
                    bestDist = dist;
                    bestTarget = tile;
                }
            }
        }

        // Move toward the target tile for improvement
        if (bestTarget != builder.position && bestDist < std::numeric_limits<int32_t>::max()) {
            targetedTiles.insert(bestTarget);
            orderUnitMove(*builder.ptr, bestTarget, grid);
            moveUnitAlongPath(gameState, *builder.ptr, grid);
            continue;
        }

        // Step 3: No tiles to improve -- try prospecting for hidden resources
        const int32_t prospectIdx = grid.toIndex(builder.position);
        if (grid.owner(prospectIdx) == this->m_player && canProspect(grid, prospectIdx)) {
            uint32_t rngSeed = static_cast<uint32_t>(prospectIdx) * 104729u
                             + static_cast<uint32_t>(this->m_player) * 7919u;
            const bool found = prospectTile(grid, prospectIdx, prospectTechBonus, rngSeed);
            builder.ptr->useCharge();
            if (!builder.ptr->hasCharges()) {
                gsPlayer->removeUnit(builder.ptr);
            }
            if (found) {
                LOG_INFO("AI %u Builder prospected and found resource at (%d,%d)",
                         static_cast<unsigned>(this->m_player),
                         builder.position.q, builder.position.r);
                aoc::sim::VisibilityEvent ev{};
                ev.type = aoc::sim::VisibilityEventType::ResourceRevealed;
                ev.location = builder.position;
                ev.actor = this->m_player;
                ev.payload = static_cast<int32_t>(grid.resource(prospectIdx).value);
                gameState.visibilityBus().emit(ev);
            }
            continue;
        }

        // Find a nearby prospectable tile and move toward it
        for (const aoc::hex::AxialCoord& cityLoc : cityLocations) {
            std::vector<aoc::hex::AxialCoord> ring2Tiles;
            ring2Tiles.reserve(12);
            aoc::hex::ring(cityLoc, 2, std::back_inserter(ring2Tiles));
            aoc::hex::ring(cityLoc, 3, std::back_inserter(ring2Tiles));

            for (const aoc::hex::AxialCoord& tile : ring2Tiles) {
                if (!grid.isValid(tile)) { continue; }
                const int32_t tIdx = grid.toIndex(tile);
                if (grid.owner(tIdx) != this->m_player) { continue; }
                if (!canProspect(grid, tIdx)) { continue; }
                if (targetedTiles.find(tile) != targetedTiles.end()) { continue; }

                const int32_t dist = grid.distance(builder.position, tile);
                if (dist < bestDist) {
                    bestDist = dist;
                    bestTarget = tile;
                }
            }
        }
        if (bestTarget != builder.position && bestDist < std::numeric_limits<int32_t>::max()) {
            targetedTiles.insert(bestTarget);
            orderUnitMove(*builder.ptr, bestTarget, grid);
            moveUnitAlongPath(gameState, *builder.ptr, grid);
        }
    }
}

} // namespace aoc::sim::ai
