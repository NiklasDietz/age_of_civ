/**
 * @file ComparativeAdvantage.cpp
 * @brief Comparative advantage and opportunity cost calculations.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/simulation/economy/ComparativeAdvantage.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/ecs/World.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim {

float playerProductionRate(const aoc::game::GameState& gameState,
                            PlayerId player,
                            uint16_t goodId) {
    aoc::ecs::World& world = gameState.legacyWorld();
    // Sum production from all cities owned by this player.
    // For raw resources: count tile yields.
    // For processed goods: check if any city has the required building
    //   and access to input goods.
    // This is a simplified estimate for comparative advantage calculations.

    float totalRate = 0.0f;

    // Check tile resources
    const aoc::ecs::ComponentPool<TileResourceComponent>* tilePool =
        world.getPool<TileResourceComponent>();
    if (tilePool != nullptr) {
        for (uint32_t i = 0; i < tilePool->size(); ++i) {
            const TileResourceComponent& res = tilePool->data()[i];
            if (res.goodId == goodId) {
                // Simplified: assume player owns the tile if they have a city nearby.
                // Full implementation would check tile ownership.
                totalRate += static_cast<float>(res.currentYield);
            }
        }
    }

    // Check processed goods via recipes
    const std::vector<ProductionRecipe>& recipes = allRecipes();
    for (const ProductionRecipe& recipe : recipes) {
        if (recipe.outputGoodId != goodId) {
            continue;
        }
        // Estimate: player can produce this if they have the inputs.
        // Rate is limited by the scarcest input.
        float minInputRate = 1000.0f;
        for (const RecipeInput& input : recipe.inputs) {
            float inputRate = playerProductionRate(world, player, input.goodId);
            float neededPerUnit = static_cast<float>(input.amount);
            if (neededPerUnit > 0.0f) {
                minInputRate = std::min(minInputRate, inputRate / neededPerUnit);
            }
        }
        if (minInputRate < 1000.0f) {
            totalRate += minInputRate * static_cast<float>(recipe.outputAmount);
        }
    }

    return totalRate;
}

std::vector<TradeRecommendation> computeComparativeAdvantage(
    const aoc::game::GameState& gameState,
    const Market& market,
    PlayerId playerA,
    PlayerId playerB) {
    aoc::ecs::World& world = gameState.legacyWorld();

    std::vector<TradeRecommendation> recommendations;

    // Collect production rates for all tradeable goods
    uint16_t count = goodCount();
    std::vector<float> ratesA(count, 0.0f);
    std::vector<float> ratesB(count, 0.0f);

    float totalA = 0.0f;
    float totalB = 0.0f;

    for (uint16_t g = 0; g < count; ++g) {
        ratesA[g] = playerProductionRate(world, playerA, g);
        ratesB[g] = playerProductionRate(world, playerB, g);
        totalA += ratesA[g] * static_cast<float>(market.price(g));
        totalB += ratesB[g] * static_cast<float>(market.price(g));
    }

    if (totalA < 0.01f || totalB < 0.01f) {
        return recommendations;  // One player produces nothing
    }

    // Compute opportunity cost for each good:
    // OC(player, good) = value of total alternative production / rate of this good
    // Simplified: OC is proportional to how much other goods you give up.
    for (uint16_t g = 0; g < count; ++g) {
        if (ratesA[g] < 0.01f && ratesB[g] < 0.01f) {
            continue;  // Neither player produces this
        }

        float ocA = (ratesA[g] > 0.01f)
            ? (totalA - ratesA[g] * static_cast<float>(market.price(g))) / (ratesA[g])
            : 9999.0f;
        float ocB = (ratesB[g] > 0.01f)
            ? (totalB - ratesB[g] * static_cast<float>(market.price(g))) / (ratesB[g])
            : 9999.0f;

        // The player with LOWER opportunity cost has comparative advantage
        bool aShouldExport = (ocA < ocB);
        float gain = std::abs(ocA - ocB) * static_cast<float>(market.price(g));

        if (gain > 1.0f) {
            recommendations.push_back({g, ocA, ocB, aShouldExport, gain});
        }
    }

    // Sort by trade gain (highest first)
    std::sort(recommendations.begin(), recommendations.end(),
              [](const TradeRecommendation& a, const TradeRecommendation& b) {
                  return a.tradeGain > b.tradeGain;
              });

    return recommendations;
}

} // namespace aoc::sim
