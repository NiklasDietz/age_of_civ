/**
 * @file InternalTrade.cpp
 * @brief Internal trade between a player's own cities.
 *
 * Implements surplus/deficit matching with distance-based transport losses.
 * Creates natural internal trade lanes: mining towns feed industrial cities,
 * agricultural hinterlands feed population centers.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/economy/InternalTrade.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"

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

/// Compute effective distance between two cities, accounting for roads.
float effectiveDistance(const aoc::map::HexGrid& grid,
                       hex::AxialCoord from,
                       hex::AxialCoord to) {
    const int32_t rawDist = hex::distance(from, to);
    const float   baseDist = static_cast<float>(rawDist);

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
float transportEfficiency(float distance) {
    const float lossSteps = distance / HEXES_PER_LOSS_STEP;
    const float totalLoss = lossSteps * LOSS_PER_STEP;
    return std::max(0.0f, 1.0f - totalLoss);
}

/// Check if a city needs a specific good for any of its recipes.
bool cityNeedsGoodForRecipe(const aoc::game::City& city, uint16_t goodId) {
    const CityDistrictsComponent& districts = city.districts();

    for (const ProductionRecipe& recipe : allRecipes()) {
        if (!districts.hasBuilding(recipe.requiredBuilding)) {
            continue;
        }
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
    aoc::game::Player* playerObj = gameState.player(player);
    if (playerObj == nullptr) {
        return;
    }

    const std::vector<std::unique_ptr<aoc::game::City>>& cities = playerObj->cities();
    if (cities.size() < 2) {
        return;
    }

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

        for (std::size_t ci = 0; ci < cities.size(); ++ci) {
            if (cities[ci] == nullptr) { continue; }
            const aoc::game::City& city = *cities[ci];
            const int32_t amount = city.stockpile().getAmount(goodId);

            if (amount > SURPLUS_THRESHOLD) {
                surplusCities.push_back({ci, amount});
            } else if (amount == 0 && cityNeedsGoodForRecipe(city, goodId)) {
                deficitCities.push_back({ci});
            }
        }

        if (surplusCities.empty() || deficitCities.empty()) {
            continue;
        }

        for (const SurplusCity& surplus : surplusCities) {
            const aoc::game::City& surplusCity = *cities[surplus.cityIndex];

            float       bestDist      = 1e9f;
            std::size_t bestDeficitIdx = 0;
            bool        foundDeficit  = false;

            for (std::size_t di = 0; di < deficitCities.size(); ++di) {
                const aoc::game::City& deficitCity = *cities[deficitCities[di].cityIndex];
                const float dist = effectiveDistance(grid, surplusCity.location(),
                                                     deficitCity.location());
                if (dist < bestDist) {
                    bestDist      = dist;
                    bestDeficitIdx = di;
                    foundDeficit  = true;
                }
            }

            if (!foundDeficit) {
                continue;
            }

            const aoc::game::City& deficitCity = *cities[deficitCities[bestDeficitIdx].cityIndex];

            const int32_t transferAmount = static_cast<int32_t>(
                static_cast<float>(surplus.surplus) * TRANSFER_FRACTION);
            if (transferAmount <= 0) {
                continue;
            }

            const float   efficiency    = transportEfficiency(bestDist);
            const int32_t arrivedAmount = static_cast<int32_t>(
                static_cast<float>(transferAmount) * efficiency);
            if (arrivedAmount <= 0) {
                continue;
            }

            CityStockpileComponent& srcStockpile = cities[surplus.cityIndex]->stockpile();
            CityStockpileComponent& dstStockpile = cities[deficitCities[bestDeficitIdx].cityIndex]->stockpile();

            [[maybe_unused]] bool consumed = srcStockpile.consumeGoods(goodId, transferAmount);
            dstStockpile.addGoods(goodId, arrivedAmount);

            LOG_DEBUG("Internal trade: player %u, good %u, %d units from (%d,%d) to (%d,%d), "
                      "%d arrived (%.0f%% efficiency)",
                      static_cast<unsigned>(player), static_cast<unsigned>(goodId),
                      transferAmount,
                      surplusCity.location().q, surplusCity.location().r,
                      deficitCity.location().q, deficitCity.location().r,
                      arrivedAmount,
                      static_cast<double>(efficiency * 100.0f));
        }
    }
}

} // namespace aoc::sim
