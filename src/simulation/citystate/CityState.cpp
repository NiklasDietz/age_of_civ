/**
 * @file CityState.cpp
 * @brief City-state spawning, envoy processing, and per-turn bonuses.
 */

#include "aoc/simulation/citystate/CityState.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/city/BorderExpansion.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/ecs/World.hpp"

#include <algorithm>
#include <vector>

namespace aoc::sim {

void spawnCityStates(aoc::ecs::World& world, aoc::map::HexGrid& grid,
                      int32_t count, aoc::Random& rng) {
    const int32_t toSpawn = std::min(count, static_cast<int32_t>(CITY_STATE_COUNT));

    // Collect existing city/unit positions to ensure minimum distance
    std::vector<hex::AxialCoord> occupiedPositions;
    const aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    if (cityPool != nullptr) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            occupiedPositions.push_back(cityPool->data()[i].location);
        }
    }
    const aoc::ecs::ComponentPool<UnitComponent>* unitPool =
        world.getPool<UnitComponent>();
    if (unitPool != nullptr) {
        for (uint32_t i = 0; i < unitPool->size(); ++i) {
            occupiedPositions.push_back(unitPool->data()[i].position);
        }
    }

    const int32_t width = grid.width();
    const int32_t height = grid.height();
    constexpr int32_t MIN_DISTANCE = 8;

    for (int32_t csIdx = 0; csIdx < toSpawn; ++csIdx) {
        const CityStateDef& csDef = CITY_STATE_DEFS[static_cast<std::size_t>(csIdx)];
        const PlayerId csPlayer = static_cast<PlayerId>(CITY_STATE_PLAYER_BASE + csIdx);

        // Try to find a valid location
        hex::AxialCoord bestPos{0, 0};
        bool found = false;

        for (int32_t attempt = 0; attempt < 200; ++attempt) {
            const int32_t col = rng.nextInt(2, width - 3);
            const int32_t row = rng.nextInt(2, height - 3);
            const int32_t index = row * width + col;

            // Must be walkable land
            if (aoc::map::isWater(grid.terrain(index)) ||
                aoc::map::isImpassable(grid.terrain(index))) {
                continue;
            }

            const hex::AxialCoord candidate = hex::offsetToAxial({col, row});

            // Check minimum distance from all occupied positions
            bool tooClose = false;
            for (const hex::AxialCoord& occupied : occupiedPositions) {
                if (hex::distance(candidate, occupied) < MIN_DISTANCE) {
                    tooClose = true;
                    break;
                }
            }
            if (tooClose) {
                continue;
            }

            bestPos = candidate;
            found = true;
            break;
        }

        if (!found) {
            LOG_INFO("Could not place city-state %.*s",
                     static_cast<int>(csDef.name.size()), csDef.name.data());
            continue;
        }

        // Create city-state city entity
        EntityId csEntity = world.createEntity();

        CityComponent csCity = CityComponent::create(
            csPlayer, bestPos, std::string(csDef.name));
        csCity.population = 3;
        world.addComponent<CityComponent>(csEntity, std::move(csCity));

        ProductionQueueComponent queue{};
        world.addComponent<ProductionQueueComponent>(csEntity, std::move(queue));

        CityDistrictsComponent districts{};
        CityDistrictsComponent::PlacedDistrict center;
        center.type = DistrictType::CityCenter;
        center.location = bestPos;
        districts.districts.push_back(std::move(center));
        world.addComponent<CityDistrictsComponent>(csEntity, std::move(districts));

        // Add city-state component
        CityStateComponent csComp{};
        csComp.defId = csDef.id;
        csComp.type = csDef.type;
        csComp.location = bestPos;
        csComp.envoys.fill(0);
        csComp.suzerain = INVALID_PLAYER;
        world.addComponent<CityStateComponent>(csEntity, std::move(csComp));

        // Claim territory
        claimInitialTerritory(grid, bestPos, csPlayer);

        // Spawn a warrior for the city-state
        const std::array<hex::AxialCoord, 6> neighbors = hex::neighbors(bestPos);
        hex::AxialCoord warriorPos = bestPos;
        for (const hex::AxialCoord& nbr : neighbors) {
            if (grid.isValid(nbr) && grid.movementCost(grid.toIndex(nbr)) > 0) {
                warriorPos = nbr;
                break;
            }
        }

        EntityId warriorEntity = world.createEntity();
        world.addComponent<UnitComponent>(
            warriorEntity,
            UnitComponent::create(csPlayer, UnitTypeId{0}, warriorPos));

        occupiedPositions.push_back(bestPos);

        LOG_INFO("Spawned city-state %.*s at (%d,%d) player=%u",
                 static_cast<int>(csDef.name.size()), csDef.name.data(),
                 bestPos.q, bestPos.r, static_cast<unsigned>(csPlayer));
    }
}

void processCityStateBonuses(aoc::ecs::World& world, PlayerId player) {
    const aoc::ecs::ComponentPool<CityStateComponent>* csPool =
        world.getPool<CityStateComponent>();
    if (csPool == nullptr) {
        return;
    }

    // Find the player's economy component
    PlayerEconomyComponent* econ = nullptr;
    aoc::ecs::ComponentPool<PlayerEconomyComponent>* econPool =
        world.getPool<PlayerEconomyComponent>();
    if (econPool != nullptr) {
        for (uint32_t i = 0; i < econPool->size(); ++i) {
            if (econPool->data()[i].owner == player) {
                econ = &econPool->data()[i];
                break;
            }
        }
    }
    if (econ == nullptr) {
        return;
    }

    for (uint32_t i = 0; i < csPool->size(); ++i) {
        const CityStateComponent& cs = csPool->data()[i];
        if (player >= MAX_PLAYERS) {
            continue;
        }
        const int8_t envoyCount = cs.envoys[player];
        if (envoyCount <= 0) {
            continue;
        }

        // Determine bonus magnitude: 1 envoy = +1, 3 = +2, 6 = +3
        int32_t bonusMagnitude = 0;
        if (envoyCount >= 6) {
            bonusMagnitude = 3;
        } else if (envoyCount >= 3) {
            bonusMagnitude = 2;
        } else {
            bonusMagnitude = 1;
        }

        // Apply bonus based on city-state type
        const CurrencyAmount bonus = static_cast<CurrencyAmount>(bonusMagnitude);
        switch (cs.type) {
            case CityStateType::Militaristic:
                // Production bonus is handled via city yield; for simplicity add gold
                econ->treasury += bonus;
                break;
            case CityStateType::Scientific:
                // Science bonus -- add as gold equivalent for now
                econ->treasury += bonus;
                break;
            case CityStateType::Cultural:
                // Culture bonus -- add as gold equivalent for now
                econ->treasury += bonus;
                break;
            case CityStateType::Trade:
                econ->treasury += bonus * 2;
                break;
            case CityStateType::Religious:
                econ->treasury += bonus;
                break;
            case CityStateType::Industrial:
                econ->treasury += bonus;
                break;
            default:
                break;
        }
    }
}

} // namespace aoc::sim
