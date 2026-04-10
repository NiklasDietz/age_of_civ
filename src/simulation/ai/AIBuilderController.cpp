/**
 * @file AIBuilderController.cpp
 * @brief AI builder management: tile improvements and resource prospecting.
 */

#include "aoc/simulation/ai/AIBuilderController.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/unit/Movement.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/map/Improvement.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/ecs/World.hpp"

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

void AIBuilderController::manageBuildersAndImprovements(aoc::ecs::World& world,
                                                         aoc::map::HexGrid& grid) {
    aoc::ecs::ComponentPool<UnitComponent>* unitPool = world.getPool<UnitComponent>();
    if (unitPool == nullptr) {
        return;
    }

    // Collect owned city locations for proximity checks
    std::vector<hex::AxialCoord> cityLocations;
    const aoc::ecs::ComponentPool<CityComponent>* cityPool = world.getPool<CityComponent>();
    if (cityPool != nullptr) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            if (cityPool->data()[i].owner == this->m_player) {
                cityLocations.push_back(cityPool->data()[i].location);
            }
        }
    }

    if (cityLocations.empty()) {
        return;
    }

    // Collect all builder units owned by this player
    struct BuilderInfo {
        EntityId entity;
        UnitComponent unit;
    };
    std::vector<BuilderInfo> builders;
    for (uint32_t i = 0; i < unitPool->size(); ++i) {
        const UnitComponent& unit = unitPool->data()[i];
        if (unit.owner == this->m_player &&
            unitTypeDef(unit.typeId).unitClass == UnitClass::Civilian &&
            unit.chargesRemaining != 0) {
            builders.push_back({unitPool->entities()[i], unit});
        }
    }

    // Track tiles already targeted so multiple builders don't converge
    std::unordered_set<hex::AxialCoord> targetedTiles;

    for (const BuilderInfo& builder : builders) {
        if (!world.isAlive(builder.entity)) {
            continue;
        }
        if (builder.unit.movementRemaining <= 0) {
            continue;
        }

        // Step 1: Check if current tile can be improved
        const int32_t currentIdx = grid.toIndex(builder.unit.position);
        if (grid.owner(currentIdx) == this->m_player &&
            grid.improvement(currentIdx) == aoc::map::ImprovementType::None &&
            grid.movementCost(currentIdx) > 0) {
            const aoc::map::ImprovementType bestImpr = bestImprovementForTile(grid, currentIdx);
            if (bestImpr != aoc::map::ImprovementType::None &&
                canPlaceImprovement(grid, currentIdx, bestImpr)) {
                grid.setImprovement(currentIdx, bestImpr);
                UnitComponent* liveUnit = world.tryGetComponent<UnitComponent>(builder.entity);
                if (liveUnit != nullptr && liveUnit->chargesRemaining > 0) {
                    --liveUnit->chargesRemaining;
                    if (liveUnit->chargesRemaining == 0) {
                        world.destroyEntity(builder.entity);
                        LOG_INFO("AI %u Builder exhausted after improving (%d,%d)",
                                 static_cast<unsigned>(this->m_player),
                                 builder.unit.position.q, builder.unit.position.r);
                        continue;
                    }
                }
                LOG_INFO("AI %u Builder improved tile (%d,%d)",
                         static_cast<unsigned>(this->m_player),
                         builder.unit.position.q, builder.unit.position.r);
                continue;
            }
        }

        // Step 2: Find nearest unimproved owned tile near any city
        hex::AxialCoord bestTarget = builder.unit.position;
        int32_t bestDist = std::numeric_limits<int32_t>::max();

        for (const hex::AxialCoord& cityLoc : cityLocations) {
            std::vector<hex::AxialCoord> cityTiles;
            cityTiles.reserve(18);
            hex::ring(cityLoc, 1, std::back_inserter(cityTiles));
            hex::ring(cityLoc, 2, std::back_inserter(cityTiles));

            for (const hex::AxialCoord& tile : cityTiles) {
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

                const int32_t dist = hex::distance(builder.unit.position, tile);
                if (dist < bestDist) {
                    bestDist = dist;
                    bestTarget = tile;
                }
            }
        }

        // Move toward the target tile for improvement
        if (bestTarget != builder.unit.position && bestDist < std::numeric_limits<int32_t>::max()) {
            targetedTiles.insert(bestTarget);
            orderUnitMove(world, builder.entity, bestTarget, grid);
            moveUnitAlongPath(world, builder.entity, grid);
            continue;
        }

        // Step 3: No tiles to improve -- try prospecting for hidden resources
        const int32_t prospectIdx = grid.toIndex(builder.unit.position);
        if (grid.owner(prospectIdx) == this->m_player && canProspect(grid, prospectIdx)) {
            float prospectTechBonus = 0.0f;
            const aoc::ecs::ComponentPool<PlayerTechComponent>* techPool =
                world.getPool<PlayerTechComponent>();
            if (techPool != nullptr) {
                for (uint32_t ti = 0; ti < techPool->size(); ++ti) {
                    if (techPool->data()[ti].owner == this->m_player) {
                        if (techPool->data()[ti].hasResearched(TechId{10})) {
                            prospectTechBonus = 0.15f;
                        }
                        break;
                    }
                }
            }
            uint32_t rngSeed = static_cast<uint32_t>(prospectIdx) * 104729u
                             + static_cast<uint32_t>(this->m_player) * 7919u;
            bool found = prospectTile(grid, prospectIdx, prospectTechBonus, rngSeed);
            UnitComponent* liveUnit = world.tryGetComponent<UnitComponent>(builder.entity);
            if (liveUnit != nullptr && liveUnit->chargesRemaining > 0) {
                --liveUnit->chargesRemaining;
                if (liveUnit->chargesRemaining == 0) {
                    world.destroyEntity(builder.entity);
                }
            }
            if (found) {
                LOG_INFO("AI %u Builder prospected and found resource at (%d,%d)",
                         static_cast<unsigned>(this->m_player),
                         builder.unit.position.q, builder.unit.position.r);
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

                const int32_t dist = aoc::hex::distance(builder.unit.position, tile);
                if (dist < bestDist) {
                    bestDist = dist;
                    bestTarget = tile;
                }
            }
        }
        if (bestTarget != builder.unit.position && bestDist < std::numeric_limits<int32_t>::max()) {
            targetedTiles.insert(bestTarget);
            orderUnitMove(world, builder.entity, bestTarget, grid);
            moveUnitAlongPath(world, builder.entity, grid);
        }
    }
}

} // namespace aoc::sim::ai
