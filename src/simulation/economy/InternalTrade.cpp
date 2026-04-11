/**
 * @file InternalTrade.cpp
 * @brief Internal trade between a player's own cities.
 *
 * Implements surplus/deficit matching with distance-based transport losses.
 * Creates natural internal trade lanes: mining towns feed industrial cities,
 * agricultural hinterlands feed population centers.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/simulation/economy/InternalTrade.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/ecs/World.hpp"

#include <algorithm>
#include <vector>

namespace aoc::sim {

namespace {

/// Minimum stockpile to be considered a surplus city for a good.
constexpr int32_t SURPLUS_THRESHOLD = 2;

/// Fraction of surplus that can be transferred per turn.
constexpr float TRANSFER_FRACTION = 0.5f;

/// Distance (in hexes) per 10% transport loss.
constexpr float HEXES_PER_LOSS_STEP = 5.0f;

/// Loss fraction per step (10%).
constexpr float LOSS_PER_STEP = 0.10f;

/// Road discount: roads reduce effective distance by this factor.
constexpr float ROAD_DISTANCE_FACTOR = 0.5f;

struct CityInfo {
    EntityId   entity;
    PlayerId   owner;
    hex::AxialCoord location;
};

/// Compute effective distance between two cities, accounting for roads.
/// If both cities have a road on their tile, the distance is halved.
float effectiveDistance(const aoc::map::HexGrid& grid,
                       hex::AxialCoord from,
                       hex::AxialCoord to) {
    const int32_t rawDist = hex::distance(from, to);
    const float baseDist = static_cast<float>(rawDist);

    // Check if source and destination tiles have roads
    if (grid.isValid(from) && grid.isValid(to)) {
        const int32_t fromIdx = grid.toIndex(from);
        const int32_t toIdx   = grid.toIndex(to);
        if (grid.hasRoad(fromIdx) && grid.hasRoad(toIdx)) {
            return baseDist * ROAD_DISTANCE_FACTOR;
        }
    }

    return baseDist;
}

/// Compute transport efficiency for a given effective distance.
/// Returns a value in [0.0, 1.0] representing the fraction of goods that arrive.
float transportEfficiency(float distance) {
    const float lossSteps = distance / HEXES_PER_LOSS_STEP;
    const float totalLoss = lossSteps * LOSS_PER_STEP;
    return std::max(0.0f, 1.0f - totalLoss);
}

/// Check if a city needs a specific good for any of its recipes.
bool cityNeedsGoodForRecipe(const aoc::game::GameState& gameState,
                            EntityId cityEntity,
                            uint16_t goodId) {
    aoc::ecs::World& world = gameState.legacyWorld();
    const CityDistrictsComponent* districts =
        world.tryGetComponent<CityDistrictsComponent>(cityEntity);
    if (districts == nullptr) {
        return false;
    }

    for (const ProductionRecipe& recipe : allRecipes()) {
        // Check if the city has the building required for this recipe
        if (!districts->hasBuilding(recipe.requiredBuilding)) {
            continue;
        }
        // Check if this recipe needs the good as input
        for (const RecipeInput& input : recipe.inputs) {
            if (input.goodId == goodId) {
                return true;
            }
        }
    }
    return false;
}

} // anonymous namespace

void processInternalTrade(aoc::game::GameState& gameState,
                          const aoc::map::HexGrid& grid,
                          PlayerId player) {
    aoc::ecs::World& world = gameState.legacyWorld();
    // Gather all cities belonging to this player
    aoc::ecs::ComponentPool<CityComponent>* cityPool = world.getPool<CityComponent>();
    if (cityPool == nullptr) {
        return;
    }

    std::vector<CityInfo> playerCities;
    for (uint32_t i = 0; i < cityPool->size(); ++i) {
        const CityComponent& city = cityPool->data()[i];
        if (city.owner == player) {
            playerCities.push_back({cityPool->entities()[i], city.owner, city.location});
        }
    }

    // Need at least 2 cities for internal trade
    if (playerCities.size() < 2) {
        return;
    }

    // For each good type, find surplus and deficit cities
    const uint16_t totalGoods = goodCount();

    for (uint16_t goodId = 0; goodId < totalGoods; ++goodId) {
        struct SurplusCity {
            std::size_t cityIndex;
            int32_t     surplus;
        };
        struct DeficitCity {
            std::size_t cityIndex;
        };

        std::vector<SurplusCity> surplusCities;
        std::vector<DeficitCity> deficitCities;

        for (std::size_t ci = 0; ci < playerCities.size(); ++ci) {
            const CityInfo& info = playerCities[ci];
            const CityStockpileComponent* stockpile =
                world.tryGetComponent<CityStockpileComponent>(info.entity);

            const int32_t amount = (stockpile != nullptr) ? stockpile->getAmount(goodId) : 0;

            if (amount > SURPLUS_THRESHOLD) {
                surplusCities.push_back({ci, amount});
            } else if (amount == 0 && cityNeedsGoodForRecipe(world, info.entity, goodId)) {
                deficitCities.push_back({ci});
            }
        }

        // No transfers needed if no surplus-deficit pairs exist
        if (surplusCities.empty() || deficitCities.empty()) {
            continue;
        }

        // Match each surplus city to the nearest deficit city and transfer
        for (const SurplusCity& surplus : surplusCities) {
            const CityInfo& surplusInfo = playerCities[surplus.cityIndex];

            // Find nearest deficit city
            float bestDist = 1e9f;
            std::size_t bestDeficitIdx = 0;
            bool foundDeficit = false;

            for (std::size_t di = 0; di < deficitCities.size(); ++di) {
                const CityInfo& deficitInfo = playerCities[deficitCities[di].cityIndex];
                const float dist = effectiveDistance(grid, surplusInfo.location,
                                                    deficitInfo.location);
                if (dist < bestDist) {
                    bestDist = dist;
                    bestDeficitIdx = di;
                    foundDeficit = true;
                }
            }

            if (!foundDeficit) {
                continue;
            }

            const CityInfo& deficitInfo = playerCities[deficitCities[bestDeficitIdx].cityIndex];

            // Compute transfer amount: half of surplus
            const int32_t transferAmount = static_cast<int32_t>(
                static_cast<float>(surplus.surplus) * TRANSFER_FRACTION);
            if (transferAmount <= 0) {
                continue;
            }

            // Apply transport loss
            const float efficiency = transportEfficiency(bestDist);
            const int32_t arrivedAmount = static_cast<int32_t>(
                static_cast<float>(transferAmount) * efficiency);
            if (arrivedAmount <= 0) {
                continue;
            }

            // Execute the transfer
            CityStockpileComponent* srcStockpile =
                world.tryGetComponent<CityStockpileComponent>(surplusInfo.entity);
            CityStockpileComponent* dstStockpile =
                world.tryGetComponent<CityStockpileComponent>(deficitInfo.entity);

            if (srcStockpile == nullptr || dstStockpile == nullptr) {
                continue;
            }

            // Deduct from source (full amount, transport loss is "wasted")
            [[maybe_unused]] bool consumed = srcStockpile->consumeGoods(goodId, transferAmount);
            // Add to destination (reduced by transport loss)
            dstStockpile->addGoods(goodId, arrivedAmount);

            LOG_DEBUG("Internal trade: player %u, good %u, %d units from (%d,%d) to (%d,%d), "
                      "%d arrived (%.0f%% efficiency)",
                      static_cast<unsigned>(player), static_cast<unsigned>(goodId),
                      transferAmount,
                      surplusInfo.location.q, surplusInfo.location.r,
                      deficitInfo.location.q, deficitInfo.location.r,
                      arrivedAmount,
                      static_cast<double>(efficiency * 100.0f));
        }
    }
}

} // namespace aoc::sim
