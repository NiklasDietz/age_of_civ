/**
 * @file AIController.cpp
 * @brief AI decision-making implementation.
 */

#include "aoc/simulation/ai/AIController.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/unit/Movement.hpp"
#include "aoc/simulation/unit/Combat.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/tech/CivicTree.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Pathfinding.hpp"
#include "aoc/ecs/World.hpp"

#include <algorithm>
#include <cstdio>

namespace aoc::sim::ai {

AIController::AIController(PlayerId player)
    : m_player(player)
{
}

void AIController::executeTurn(aoc::ecs::World& world,
                                const aoc::map::HexGrid& grid,
                                DiplomacyManager& diplomacy,
                                const Market& market,
                                aoc::Random& rng) {
    this->selectResearch(world);
    this->executeCityActions(world, grid);
    this->executeUnitActions(world, grid, rng);
    this->executeDiplomacyActions(world, diplomacy, market);

    // Refresh movement so units have moved for this turn
    refreshMovement(world, this->m_player);
}

// ============================================================================
// Research selection: pick the cheapest available tech/civic
// ============================================================================

void AIController::selectResearch(aoc::ecs::World& world) {
    world.forEach<PlayerTechComponent>(
        [this](EntityId, PlayerTechComponent& tech) {
            if (tech.owner != this->m_player) {
                return;
            }
            if (tech.currentResearch.isValid()) {
                return;  // Already researching
            }
            std::vector<TechId> available = tech.availableTechs();
            if (available.empty()) {
                return;
            }
            // Pick cheapest
            TechId best = available[0];
            int32_t bestCost = techDef(best).researchCost;
            for (std::size_t i = 1; i < available.size(); ++i) {
                int32_t cost = techDef(available[i]).researchCost;
                if (cost < bestCost) {
                    bestCost = cost;
                    best = available[i];
                }
            }
            tech.currentResearch = best;
            std::fprintf(stdout, "[AI %u] Researching: %.*s\n",
                         static_cast<unsigned>(this->m_player),
                         static_cast<int>(techDef(best).name.size()),
                         techDef(best).name.data());
        });

    world.forEach<PlayerCivicComponent>(
        [this](EntityId, PlayerCivicComponent& civic) {
            if (civic.owner != this->m_player) {
                return;
            }
            if (civic.currentResearch.isValid()) {
                return;
            }
            uint16_t count = civicCount();
            CivicId best{};
            int32_t bestCost = 999999;
            for (uint16_t i = 0; i < count; ++i) {
                CivicId id{i};
                if (civic.canResearch(id)) {
                    int32_t cost = civicDef(id).cultureCost;
                    if (cost < bestCost) {
                        bestCost = cost;
                        best = id;
                    }
                }
            }
            if (best.isValid()) {
                civic.currentResearch = best;
            }
        });
}

// ============================================================================
// Unit actions: settlers found cities, military units explore or attack
// ============================================================================

void AIController::executeUnitActions(aoc::ecs::World& world,
                                       const aoc::map::HexGrid& grid,
                                       aoc::Random& rng) {
    aoc::ecs::ComponentPool<UnitComponent>* unitPool = world.getPool<UnitComponent>();
    if (unitPool == nullptr) {
        return;
    }

    // Collect owned units (iterate by copy since we may modify during iteration)
    struct UnitInfo {
        EntityId entity;
        UnitComponent unit;
    };
    std::vector<UnitInfo> ownedUnits;
    for (uint32_t i = 0; i < unitPool->size(); ++i) {
        if (unitPool->data()[i].owner == this->m_player) {
            ownedUnits.push_back({unitPool->entities()[i], unitPool->data()[i]});
        }
    }

    for (const UnitInfo& info : ownedUnits) {
        if (!world.isAlive(info.entity)) {
            continue;
        }

        const UnitTypeDef& def = unitTypeDef(info.unit.typeId);

        // Settlers: found city at current location
        if (def.unitClass == UnitClass::Settler) {
            EntityId cityEntity = world.createEntity();
            world.addComponent<CityComponent>(
                cityEntity,
                CityComponent::create(this->m_player, info.unit.position, "AI City"));
            world.destroyEntity(info.entity);
            std::fprintf(stdout, "[AI %u] Founded city at (%d,%d)\n",
                         static_cast<unsigned>(this->m_player),
                         info.unit.position.q, info.unit.position.r);
            continue;
        }

        // Military/scout: explore by moving to a random nearby passable tile
        if (info.unit.movementRemaining > 0) {
            std::array<hex::AxialCoord, 6> neighbors = hex::neighbors(info.unit.position);

            // Check for adjacent enemies to attack
            bool attacked = false;
            for (const hex::AxialCoord& nbr : neighbors) {
                if (!grid.isValid(nbr)) {
                    continue;
                }
                // Find enemy unit on this tile
                aoc::ecs::ComponentPool<UnitComponent>* pool = world.getPool<UnitComponent>();
                if (pool == nullptr) {
                    break;
                }
                for (uint32_t j = 0; j < pool->size(); ++j) {
                    if (pool->data()[j].position == nbr && pool->data()[j].owner != this->m_player) {
                        resolveMeleeCombat(world, rng, grid, info.entity, pool->entities()[j]);
                        attacked = true;
                        break;
                    }
                }
                if (attacked) {
                    break;
                }
            }

            if (!attacked && world.isAlive(info.entity)) {
                // Move to a random valid neighbor
                std::vector<hex::AxialCoord> validMoves;
                for (const hex::AxialCoord& nbr : neighbors) {
                    if (grid.isValid(nbr) && grid.movementCost(grid.toIndex(nbr)) > 0) {
                        validMoves.push_back(nbr);
                    }
                }
                if (!validMoves.empty()) {
                    int32_t idx = rng.nextInt(0, static_cast<int32_t>(validMoves.size()) - 1);
                    orderUnitMove(world, info.entity, validMoves[static_cast<std::size_t>(idx)], grid);
                    moveUnitAlongPath(world, info.entity, grid);
                }
            }
        }
    }
}

// ============================================================================
// City actions: auto-build units if possible
// ============================================================================

void AIController::executeCityActions(aoc::ecs::World& world,
                                       const aoc::map::HexGrid& /*grid*/) {
    // Simple: if AI has no military units, produce a warrior at the first city
    aoc::ecs::ComponentPool<CityComponent>* cityPool = world.getPool<CityComponent>();
    if (cityPool == nullptr) {
        return;
    }

    int32_t militaryCount = 0;
    aoc::ecs::ComponentPool<UnitComponent>* unitPool = world.getPool<UnitComponent>();
    if (unitPool != nullptr) {
        for (uint32_t i = 0; i < unitPool->size(); ++i) {
            if (unitPool->data()[i].owner == this->m_player) {
                const UnitTypeDef& def = unitTypeDef(unitPool->data()[i].typeId);
                if (def.unitClass == UnitClass::Melee || def.unitClass == UnitClass::Ranged ||
                    def.unitClass == UnitClass::Cavalry) {
                    ++militaryCount;
                }
            }
        }
    }

    // Produce a warrior if we have fewer than 2 military units
    if (militaryCount < 2) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            CityComponent& city = cityPool->data()[i];
            if (city.owner != this->m_player) {
                continue;
            }
            // Instant-produce for now (proper production queue integration later)
            EntityId warrior = world.createEntity();
            world.addComponent<UnitComponent>(
                warrior,
                UnitComponent::create(this->m_player, UnitTypeId{0}, city.location));
            std::fprintf(stdout, "[AI %u] Produced warrior at %s\n",
                         static_cast<unsigned>(this->m_player), city.name.c_str());
            break;  // One unit per turn
        }
    }
}

// ============================================================================
// Diplomacy: simple heuristics
// ============================================================================

void AIController::executeDiplomacyActions(aoc::ecs::World& /*world*/,
                                            DiplomacyManager& /*diplomacy*/,
                                            const Market& /*market*/) {
    // Placeholder: AI diplomacy will evaluate relations and propose deals.
    // For now, AI is passive diplomatically.
}

// ============================================================================
// Action scoring (for future expansion)
// ============================================================================

std::vector<ScoredAction> AIController::evaluateActions(
    const aoc::ecs::World& /*world*/,
    const aoc::map::HexGrid& /*grid*/,
    const DiplomacyManager& /*diplomacy*/,
    const Market& /*market*/) const {
    // Full utility-based evaluation will be implemented as the game matures.
    // For now, actions are hard-coded priorities in executeTurn().
    return {};
}

} // namespace aoc::sim::ai
