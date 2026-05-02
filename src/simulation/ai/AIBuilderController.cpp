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
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/event/VisibilityEvents.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"

#include <cmath>
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

        // Step 1c (WP-C3): lay a PowerPole on the builder's current tile
        // when owner has researched Electricity and the tile is on a
        // well-improved owned slab adjacent to a city / existing pole.
        // Cheap heuristic: post-Electricity, if tile is owned, has any
        // improvement (so poles thread along worked tiles), and no pole
        // yet AND any neighbor tile is either a city center or already
        // has a pole — lay a pole. Bootstraps the grid from each city
        // outward. Consumes one charge.
        if (gsPlayer->hasResearched(TechId{14})
            && grid.owner(currentIdx) == this->m_player
            && !grid.hasPowerPole(currentIdx)) {
            bool adjacentToCityOrPole = false;
            for (const aoc::hex::AxialCoord& cityLoc : cityLocations) {
                if (grid.distance(cityLoc, builder.position) <= 1) {
                    adjacentToCityOrPole = true;
                    break;
                }
            }
            if (!adjacentToCityOrPole) {
                const std::array<aoc::hex::AxialCoord, 6> polNbrs =
                    aoc::hex::neighbors(builder.position);
                for (const aoc::hex::AxialCoord& n : polNbrs) {
                    if (!grid.isValid(n)) { continue; }
                    if (grid.hasPowerPole(grid.toIndex(n))) {
                        adjacentToCityOrPole = true;
                        break;
                    }
                }
            }
            if (adjacentToCityOrPole) {
                grid.setPowerPole(currentIdx, true);
                builder.ptr->useCharge();
                LOG_INFO("AI %u Builder laid PowerPole at (%d,%d)",
                         static_cast<unsigned>(this->m_player),
                         builder.position.q, builder.position.r);
                if (!builder.ptr->hasCharges()) {
                    gsPlayer->removeUnit(builder.ptr);
                }
                continue;
            }
        }

        // Step 1e-pre (WP-C4): build a Greenhouse on a blank grassland/
        // plains tile the player owns, post-Advanced-Chemistry. Only fires
        // every ~8th tile so Greenhouses don't blanket the map. Bypasses
        // `bestImprovementForTile` (which never picks Greenhouse).
        if (gsPlayer->hasResearched(TechId{24})
            && grid.owner(currentIdx) == this->m_player
            && grid.improvement(currentIdx) == aoc::map::ImprovementType::None
            && !grid.resource(currentIdx).isValid()
            && (currentIdx % 8 == 3)
            && canPlaceImprovement(grid, currentIdx,
                                   aoc::map::ImprovementType::Greenhouse)) {
            grid.setImprovement(currentIdx, aoc::map::ImprovementType::Greenhouse);
            builder.ptr->useCharge();
            LOG_INFO("AI %u Builder built Greenhouse at (%d,%d)",
                     static_cast<unsigned>(this->m_player),
                     builder.position.q, builder.position.r);
            if (!builder.ptr->hasCharges()) {
                gsPlayer->removeUnit(builder.ptr);
            }
            continue;
        }

        // Step 1e (WP-C4): seed a Greenhouse the unit stands on. If the
        // tile is a Greenhouse with no planted crop yet, auto-plant a
        // climate-banded good the empire has in stock. Prefers the most
        // abundant so the AI uses surplus trade goods rather than scarce
        // natives. Consumes 1 seed. Skipped if no stock or already planted.
        if (grid.improvement(currentIdx) == aoc::map::ImprovementType::Greenhouse
            && grid.greenhouseCrop(currentIdx) == 0xFFFFu) {
            uint16_t bestGood = 0xFFFFu;
            int32_t bestStock = 0;
            aoc::game::City* sourceCity = nullptr;
            for (const std::unique_ptr<aoc::game::City>& c : gsPlayer->cities()) {
                for (uint16_t gid = 0; gid < aoc::sim::goodCount(); ++gid) {
                    if (aoc::sim::goodDef(gid).climateBand
                        == aoc::sim::ClimateBand::Any) { continue; }
                    const int32_t stock = c->stockpile().getAmount(gid);
                    if (stock > bestStock) {
                        bestStock = stock;
                        bestGood = gid;
                        sourceCity = c.get();
                    }
                }
            }
            if (bestGood != 0xFFFFu && sourceCity != nullptr) {
                if (aoc::sim::plantGreenhouseCrop(
                        grid, sourceCity->stockpile(), currentIdx, bestGood)) {
                    builder.ptr->useCharge();
                    LOG_INFO("AI %u Builder planted crop %u in Greenhouse",
                             static_cast<unsigned>(this->m_player),
                             static_cast<unsigned>(bestGood));
                    if (!builder.ptr->hasCharges()) {
                        gsPlayer->removeUnit(builder.ptr);
                    }
                    continue;
                }
            }
        }

        // Step 1d (WP-C3): lay a Pipeline along proven oil/gas tiles.
        // Post-Mass-Production (TechId 15), if builder stands on an owned
        // tile whose resource is OIL or NATURAL_GAS (or hasn't yet been
        // piped AND a neighbor already has a pipeline) — lay a pipeline.
        // Bootstraps pipelines outward from extraction tiles so traders
        // get the hauling speed bonus on the routes that matter.
        if (gsPlayer->hasResearched(TechId{15})
            && grid.owner(currentIdx) == this->m_player
            && !grid.hasPipeline(currentIdx)) {
            bool atExtractionTile = false;
            const ResourceId res = grid.resource(currentIdx);
            if (res.isValid()
                && (res.value == aoc::sim::goods::OIL
                    || res.value == aoc::sim::goods::NATURAL_GAS)) {
                atExtractionTile = true;
            }
            bool adjacentToPipeline = false;
            if (!atExtractionTile) {
                const std::array<aoc::hex::AxialCoord, 6> pipNbrs =
                    aoc::hex::neighbors(builder.position);
                for (const aoc::hex::AxialCoord& n : pipNbrs) {
                    if (!grid.isValid(n)) { continue; }
                    if (grid.hasPipeline(grid.toIndex(n))) {
                        adjacentToPipeline = true;
                        break;
                    }
                }
            }
            if (atExtractionTile || adjacentToPipeline) {
                grid.setPipeline(currentIdx, true);
                builder.ptr->useCharge();
                LOG_INFO("AI %u Builder laid Pipeline at (%d,%d)",
                         static_cast<unsigned>(this->m_player),
                         builder.position.q, builder.position.r);
                if (!builder.ptr->hasCharges()) {
                    gsPlayer->removeUnit(builder.ptr);
                }
                continue;
            }
        }

        // Step 1g (WP-K7): place a Trading Post on the current tile if
        // it's a neutral (unowned) passable land tile within 12 hex of any
        // owned city. Post-Currency (TechId 5). Boosts trade range relay
        // outside the civ's borders.
        // 2026-05-02: any passable land terrain (was Desert/Plains only),
        // closeOwn cap 8 → 12, post cap 3 → 8 per civ.
        // Overbuild allowed on neutral tiles: another civ's stale improvement
        // (e.g. razed-city remnant or rival relay) can be replaced. Tile
        // ownership stays unowned; the new improvement just supersedes the
        // old. Trading Post relays work for everyone regardless of placer.
        if (gsPlayer->hasResearched(TechId{5})
            && grid.owner(currentIdx) == INVALID_PLAYER
            // No-op overbuild: skip tiles that already host a Trading Post.
            // Posts are shared infrastructure; replacing a post with a post
            // wastes the builder's charge.
            && grid.improvement(currentIdx) != aoc::map::ImprovementType::TradingPost) {
            const aoc::map::TerrainType t = grid.terrain(currentIdx);
            const bool passableLand = !aoc::map::isWater(t)
                                   && t != aoc::map::TerrainType::Mountain
                                   && grid.movementCost(currentIdx) > 0;
            if (passableLand) {
                int32_t closeOwn = std::numeric_limits<int32_t>::max();
                for (const aoc::hex::AxialCoord& c : cityLocations) {
                    closeOwn = std::min(closeOwn, grid.distance(builder.position, c));
                }
                int32_t existingPosts = 0;
                const int32_t tilesN = grid.tileCount();
                for (int32_t ti = 0; ti < tilesN; ++ti) {
                    if (grid.improvement(ti) == aoc::map::ImprovementType::TradingPost) {
                        ++existingPosts;
                    }
                }
                if (closeOwn <= 12 && closeOwn >= 2 && existingPosts < 8 + this->m_player) {
                    if (canPlaceImprovement(grid, currentIdx,
                                            aoc::map::ImprovementType::TradingPost)) {
                        grid.setImprovement(currentIdx,
                                            aoc::map::ImprovementType::TradingPost);
                        builder.ptr->useCharge();
                        LOG_INFO("AI %u Builder placed TradingPost relay at (%d,%d)",
                                 static_cast<unsigned>(this->m_player),
                                 builder.position.q, builder.position.r);
                        if (!builder.ptr->hasCharges()) {
                            gsPlayer->removeUnit(builder.ptr);
                        }
                        continue;
                    }
                }
            }
        }

        // Step 1h (WP-S2): place an Encampment on current owned land tile
        // if it's near the border (within 4 hex of foreign territory) and
        // no encampment exists within 6 hex. Engineering tech (6).
        if (gsPlayer->hasResearched(TechId{6})
            && grid.owner(currentIdx) == this->m_player
            && grid.improvement(currentIdx) == aoc::map::ImprovementType::None) {
            // Border check: any tile within 4 hex owned by another player.
            bool nearBorder = false;
            const std::array<aoc::hex::AxialCoord, 6> nbrs1 = aoc::hex::neighbors(builder.position);
            for (const aoc::hex::AxialCoord& nbr : nbrs1) {
                if (!grid.isValid(nbr)) { continue; }
                const PlayerId own = grid.owner(grid.toIndex(nbr));
                if (own != this->m_player && own != INVALID_PLAYER) {
                    nearBorder = true;
                    break;
                }
            }
            if (nearBorder) {
                // Avoid encampment spam: check 6-hex radius for existing depots.
                bool tooClose = false;
                const int32_t tilesN = grid.tileCount();
                for (int32_t ti = 0; ti < tilesN; ++ti) {
                    if (grid.improvement(ti) != aoc::map::ImprovementType::Encampment) { continue; }
                    if (grid.distance(builder.position, grid.toAxial(ti)) <= 6) {
                        tooClose = true;
                        break;
                    }
                }
                if (!tooClose
                 && canPlaceImprovement(grid, currentIdx,
                                        aoc::map::ImprovementType::Encampment)) {
                    grid.setImprovement(currentIdx,
                                        aoc::map::ImprovementType::Encampment);
                    builder.ptr->useCharge();
                    LOG_INFO("AI %u Builder placed Encampment at (%d,%d)",
                             static_cast<unsigned>(this->m_player),
                             builder.position.q, builder.position.r);
                    if (!builder.ptr->hasCharges()) {
                        gsPlayer->removeUnit(builder.ptr);
                    }
                    continue;
                }
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

        // Step 2b (WP-K7): deliberate relay-seek. AI drops Trading Posts
        // along trade-route paths so caravans crossing wide neutral land
        // don't fail with "longest segment > range".
        // 2026-05-02: aggressive variant — any passable land terrain
        // qualifies (was Desert/Plains only), pair-distance gate dropped
        // to >5 hex (was >8), builder-distance gate widened to 20 (was 12),
        // post cap raised to 8 + player_id (was 3 + player_id). Audit
        // showed ~3k "longest segment > range" rejections; the prior
        // restrictive variant rarely placed enough relays to clear them.
        if (gsPlayer->hasResearched(TechId{5})) {
            int32_t existingPosts = 0;
            const int32_t tilesN = grid.tileCount();
            for (int32_t ti = 0; ti < tilesN; ++ti) {
                if (grid.improvement(ti) == aoc::map::ImprovementType::TradingPost) {
                    ++existingPosts;
                }
            }
            const int32_t postCap = 8 + static_cast<int32_t>(this->m_player);
            if (existingPosts < postCap) {
                std::vector<aoc::hex::AxialCoord> foreignCities;
                for (const std::unique_ptr<aoc::game::Player>& other : gameState.players()) {
                    if (other == nullptr) { continue; }
                    if (other->id() == this->m_player) { continue; }
                    for (const std::unique_ptr<aoc::game::City>& fc : other->cities()) {
                        foreignCities.push_back(fc->location());
                    }
                }
                for (const aoc::hex::AxialCoord& fc : foreignCities) {
                    for (const aoc::hex::AxialCoord& cityLoc : cityLocations) {
                        const int32_t pairDist = grid.distance(cityLoc, fc);
                        if (pairDist <= 5 || pairDist > 40) { continue; }
                        const int32_t midSteps = pairDist / 2;
                        const float lerp = static_cast<float>(midSteps)
                                         / static_cast<float>(pairDist);
                        const int32_t mq = static_cast<int32_t>(std::round(
                            static_cast<float>(cityLoc.q) * (1.0f - lerp)
                          + static_cast<float>(fc.q) * lerp));
                        const int32_t mr = static_cast<int32_t>(std::round(
                            static_cast<float>(cityLoc.r) * (1.0f - lerp)
                          + static_cast<float>(fc.r) * lerp));
                        const aoc::hex::AxialCoord mid{mq, mr};
                        if (!grid.isValid(mid)) { continue; }
                        const int32_t midIdx = grid.toIndex(mid);
                        if (grid.owner(midIdx) != INVALID_PLAYER) { continue; }
                        // Skip tiles that already host a Trading Post — Posts
                        // are owner-agnostic relays, no value in re-placing.
                        // Other improvements may be overbuilt (rival Mine on
                        // razed-city tile etc.).
                        if (grid.improvement(midIdx)
                            == aoc::map::ImprovementType::TradingPost) { continue; }
                        if (grid.movementCost(midIdx) <= 0) { continue; }
                        // Accept any passable LAND tile (not water, not mountain).
                        const aoc::map::TerrainType tt = grid.terrain(midIdx);
                        if (aoc::map::isWater(tt)) { continue; }
                        if (tt == aoc::map::TerrainType::Mountain) { continue; }
                        if (targetedTiles.find(mid) != targetedTiles.end()) { continue; }
                        const int32_t bdist = grid.distance(builder.position, mid);
                        if (bdist > 20) { continue; }
                        // Strong priority bias: relays unlock entire trade lanes.
                        const int32_t weighted = bdist - 6;
                        if (weighted < bestDist) {
                            bestDist = weighted;
                            bestTarget = mid;
                        }
                    }
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
