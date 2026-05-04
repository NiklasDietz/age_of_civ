/**
 * @file ComparativeAdvantage.cpp
 * @brief Comparative advantage and opportunity cost calculations.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/economy/ComparativeAdvantage.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim {

float playerProductionRate(const aoc::game::GameState& gameState,
                            PlayerId player,
                            uint16_t goodId) {
    // Sum production from all cities owned by this player.
    // For raw resources: count tile yields via the legacy TileResourceComponent pool
    // (tile ownership is still tracked in the ECS).
    // For processed goods: check if any city has the required building and inputs.

    float totalRate = 0.0f;

    // Sum this player's last-turn production of the good. lastTurnProduction
    // is cleared at the top of EconomySimulation::executeProduction and
    // populated as recipes fire, so during this turn-step it holds the
    // PREVIOUS turn's totals -- exactly the rate signal we want.
    // (Renamed from totalSupply 2026-05-03; the old field accumulated
    // forever and inflated rate readings after many turns.)
    const aoc::game::Player* playerObj = gameState.player(player);
    if (playerObj != nullptr) {
        const std::unordered_map<uint16_t, int32_t>& supply = playerObj->economy().lastTurnProduction;
        const std::unordered_map<uint16_t, int32_t>::const_iterator it = supply.find(goodId);
        if (it != supply.end() && it->second > 0) {
            totalRate += static_cast<float>(it->second);
        }
    }

    const std::vector<ProductionRecipe>& recipes = allRecipes();
    for (const ProductionRecipe& recipe : recipes) {
        if (recipe.outputGoodId != goodId) { continue; }

        float minInputRate = 1000.0f;
        for (const RecipeInput& input : recipe.inputs) {
            float inputRate     = playerProductionRate(gameState, player, input.goodId);
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
    std::vector<TradeRecommendation> recommendations;

    uint16_t count = goodCount();
    std::vector<float> ratesA(count, 0.0f);
    std::vector<float> ratesB(count, 0.0f);

    float totalA = 0.0f;
    float totalB = 0.0f;

    for (uint16_t g = 0; g < count; ++g) {
        ratesA[g] = playerProductionRate(gameState, playerA, g);
        ratesB[g] = playerProductionRate(gameState, playerB, g);
        totalA   += ratesA[g] * static_cast<float>(market.price(g));
        totalB   += ratesB[g] * static_cast<float>(market.price(g));
    }

    if (totalA < 0.01f || totalB < 0.01f) {
        return recommendations;
    }

    for (uint16_t g = 0; g < count; ++g) {
        if (ratesA[g] < 0.01f && ratesB[g] < 0.01f) { continue; }

        // Opportunity cost of producing one unit of good g, expressed as
        // the market-value of all OTHER production per unit of g:
        //     oc = (totalValue - valueOfG) / unitsOfG
        //        = ($ of other goods) / (units of g)
        // Lower opportunity cost means the civ gives up less value per
        // unit of g, i.e. it has comparative advantage in g and should
        // export it. A rate below the epsilon floor is treated as
        // prohibitively expensive (9999).
        float ocA = (ratesA[g] > 0.01f)
            ? (totalA - ratesA[g] * static_cast<float>(market.price(g))) / ratesA[g]
            : 9999.0f;
        float ocB = (ratesB[g] > 0.01f)
            ? (totalB - ratesB[g] * static_cast<float>(market.price(g))) / ratesB[g]
            : 9999.0f;

        bool  aShouldExport = (ocA < ocB);
        float gain          = std::abs(ocA - ocB) * static_cast<float>(market.price(g));

        if (gain > 1.0f) {
            recommendations.push_back({g, ocA, ocB, aShouldExport, gain});
        }
    }

    std::sort(recommendations.begin(), recommendations.end(),
              [](const TradeRecommendation& a, const TradeRecommendation& b) {
                  return a.tradeGain > b.tradeGain;
              });

    return recommendations;
}

} // namespace aoc::sim
