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
#include "aoc/simulation/city/CityConnection.hpp"
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
/// Road discount applies only when a continuous road path actually exists
/// between the endpoints. Previous check only tested hasRoad() at the two
/// city tiles themselves -- since cities sit on road tiles by construction
/// that check was always true, granting the discount universally even
/// between cities separated by roadless wilderness.
float effectiveDistance(const aoc::map::HexGrid& grid,
                       hex::AxialCoord from,
                       hex::AxialCoord to) {
    const int32_t rawDist = grid.distance(from, to);
    const float   baseDist = static_cast<float>(rawDist);

    if (isCityConnected(grid, from, to)) {
        return baseDist * ROAD_DISTANCE_FACTOR;
    }

    return baseDist;
}

/// Compute transport efficiency for a given effective distance.
float transportEfficiency(float distance) {
    const float lossSteps = distance / HEXES_PER_LOSS_STEP;
    const float totalLoss = lossSteps * LOSS_PER_STEP;
    return std::max(0.0f, 1.0f - totalLoss);
}

/// How much of `goodId` this city needs on hand to run a batch of the hungriest
/// recipe it is actually equipped for. 0 if none of its recipes want the good.
///
/// This used to be a bool "does any recipe want it", which left a dead zone: a
/// city was classified as a deficit only at exactly 0, and as a surplus only
/// above 2. A Forge city sitting on 1 iron ore therefore counted as neither, so
/// it never received a top-up and could never reach the 2 ore that "Smelt Iron"
/// needs. Measured effect: Ingots stayed at 0-1 for a whole 60-turn game even
/// once ore was reaching cities.
int32_t cityRecipeNeedForGood(const aoc::game::City& city, uint16_t goodId) {
    const CityDistrictsComponent& districts = city.districts();

    int32_t needed = 0;
    for (const ProductionRecipe& recipe : allRecipes()) {
        if (!districts.hasBuilding(recipe.requiredBuilding)) {
            continue;
        }
        for (const RecipeInput& input : recipe.inputs) {
            if (input.goodId == goodId && input.amount > needed) {
                needed = input.amount;
            }
        }
    }
    return needed;
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

            const int32_t recipeNeed = cityRecipeNeedForGood(city, goodId);

            if (amount > SURPLUS_THRESHOLD && amount > recipeNeed) {
                // Never ship away the working stock a city needs for its own
                // recipes -- a Forge city used to export the very ore it was
                // about to smelt.
                surplusCities.push_back({ci, amount});
            } else if (recipeNeed > 0 && amount < recipeNeed) {
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

            [[maybe_unused]] const aoc::game::City& deficitCity =
                *cities[deficitCities[bestDeficitIdx].cityIndex];

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

            if (!srcStockpile.consumeGoods(goodId, transferAmount)) {
                LOG_WARN("Internal trade: consumeGoods failed for good %u at (%d,%d) "
                         "despite prior surplus check (player %u)",
                         static_cast<unsigned>(goodId),
                         surplusCity.location().q, surplusCity.location().r,
                         static_cast<unsigned>(player));
                continue;
            }
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
