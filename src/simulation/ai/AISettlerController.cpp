/**
 * @file AISettlerController.cpp
 * @brief AI settler management: city location scoring, settler movement,
 *        and city founding.
 */

#include "aoc/simulation/ai/AISettlerController.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/unit/Movement.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/city/BorderExpansion.hpp"
#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/ecs/World.hpp"

namespace aoc::sim::ai {

// ============================================================================
// Helper: Score a potential city location for settler placement.
// ============================================================================

static float scoreCityLocation(hex::AxialCoord pos,
                               const aoc::map::HexGrid& grid,
                               const aoc::ecs::World& world,
                               PlayerId player) {
    if (!grid.isValid(pos)) {
        return -1000.0f;
    }
    const int32_t centerIdx = grid.toIndex(pos);

    if (aoc::map::isWater(grid.terrain(centerIdx)) ||
        aoc::map::isImpassable(grid.terrain(centerIdx))) {
        return -1000.0f;
    }

    if (grid.owner(centerIdx) != INVALID_PLAYER) {
        return -500.0f;
    }

    float score = 0.0f;

    const std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(pos);
    int32_t coastCount = 0;
    for (const hex::AxialCoord& nbr : nbrs) {
        if (!grid.isValid(nbr)) {
            continue;
        }
        const int32_t nbrIdx = grid.toIndex(nbr);
        const aoc::map::TileYield yield = grid.tileYield(nbrIdx);
        score += static_cast<float>(yield.food) * 2.0f;
        score += static_cast<float>(yield.production) * 1.5f;
        score += static_cast<float>(yield.gold) * 1.0f;

        if (aoc::map::isWater(grid.terrain(nbrIdx))) {
            ++coastCount;
        }

        if (grid.resource(nbrIdx).isValid()) {
            score += 3.0f;
        }
    }

    if (coastCount > 0 && coastCount <= 3) {
        score += 4.0f;
    }

    const aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    if (cityPool != nullptr) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            const CityComponent& city = cityPool->data()[i];
            const int32_t dist = hex::distance(pos, city.location);

            if (city.owner == player) {
                if (dist < 4) {
                    score -= 50.0f;
                } else if (dist > 8) {
                    score -= static_cast<float>(dist - 8) * 2.0f;
                } else if (dist >= 4 && dist <= 6) {
                    score += 5.0f;
                }
            } else {
                if (dist < 4) {
                    score -= 30.0f;
                }
            }
        }
    }

    const aoc::map::TileYield centerYield = grid.tileYield(centerIdx);
    score += static_cast<float>(centerYield.food) * 1.5f;
    score += static_cast<float>(centerYield.production) * 1.0f;

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
// Settler actions
// ============================================================================

void AISettlerController::executeSettlerActions(aoc::ecs::World& world,
                                                aoc::map::HexGrid& grid) {
    aoc::ecs::ComponentPool<UnitComponent>* unitPool = world.getPool<UnitComponent>();
    if (unitPool == nullptr) {
        return;
    }

    // Collect settler units (copy because founding destroys the entity)
    struct SettlerInfo {
        EntityId entity;
        UnitComponent unit;
    };
    std::vector<SettlerInfo> settlers;
    for (uint32_t i = 0; i < unitPool->size(); ++i) {
        if (unitPool->data()[i].owner == this->m_player &&
            unitTypeDef(unitPool->data()[i].typeId).unitClass == UnitClass::Settler) {
            settlers.push_back({unitPool->entities()[i], unitPool->data()[i]});
        }
    }

    for (const SettlerInfo& info : settlers) {
        if (!world.isAlive(info.entity)) {
            continue;
        }

        // Score candidate locations in a radius around the settler
        float bestScore = -999.0f;
        hex::AxialCoord bestLocation = info.unit.position;

        std::vector<hex::AxialCoord> candidates;
        candidates.reserve(200);
        hex::spiral(info.unit.position, 8, std::back_inserter(candidates));

        for (const hex::AxialCoord& candidate : candidates) {
            if (!grid.isValid(candidate)) {
                continue;
            }
            const float locationScore = scoreCityLocation(
                candidate, grid, world, this->m_player);
            if (locationScore > bestScore) {
                bestScore = locationScore;
                bestLocation = candidate;
            }
        }

        // Found city if at the best location
        if (bestLocation == info.unit.position && bestScore > -100.0f) {
            EntityId cityEntity = world.createEntity();
            const std::string aiCityName = getNextCityName(world, this->m_player);
            world.addComponent<CityComponent>(
                cityEntity,
                CityComponent::create(this->m_player, info.unit.position, aiCityName));
            world.addComponent<ProductionQueueComponent>(
                cityEntity, ProductionQueueComponent{});

            CityDistrictsComponent districts{};
            CityDistrictsComponent::PlacedDistrict center;
            center.type = DistrictType::CityCenter;
            center.location = info.unit.position;
            districts.districts.push_back(std::move(center));
            world.addComponent<CityDistrictsComponent>(
                cityEntity, std::move(districts));

            world.addComponent<CityReligionComponent>(
                cityEntity, CityReligionComponent{});

            claimInitialTerritory(grid, info.unit.position, this->m_player);
            world.destroyEntity(info.entity);
            LOG_INFO("AI %u Founded city at (%d,%d) score=%.1f",
                     static_cast<unsigned>(this->m_player),
                     info.unit.position.q, info.unit.position.r,
                     static_cast<double>(bestScore));
            continue;
        }

        // Move toward the best location
        if (bestLocation != info.unit.position && info.unit.movementRemaining > 0) {
            orderUnitMove(world, info.entity, bestLocation, grid);
            moveUnitAlongPath(world, info.entity, grid);
        }
    }
}

} // namespace aoc::sim::ai
