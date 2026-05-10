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
#include <unordered_set>
#include <vector>

namespace aoc::sim {

namespace {

/// Recursive helper. Tracks the goods currently being expanded on the
/// call stack so we can detect (and break) recipe DAG cycles. Recycling
/// recipes (melt-coins-back-to-ore, see ProductionChain.cpp) intentionally
/// form cycles with forward recipes — without a `visited` guard they would
/// blow the stack the moment one is added to the recipe table, which the
/// audit flagged as a release-build crash hazard.
float computeRate(const aoc::game::GameState& gameState,
                  PlayerId player,
                  uint16_t goodId,
                  std::unordered_set<uint16_t>& visited) {
    if (!visited.insert(goodId).second) {
        // Already on the recursion stack (cycle through recycling recipes
        // or a future configuration error). Treat as already-counted; the
        // outer call already accumulated this good's lastTurnProduction.
        return 0.0f;
    }

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
        // Skip recycling recipes: they form intentional cycles with forward
        // recipes and would double-count via lastTurnProduction.
        if (recipe.isRecycling) { continue; }

        float minInputRate = 1000.0f;
        for (const RecipeInput& input : recipe.inputs) {
            float inputRate     = computeRate(gameState, player, input.goodId, visited);
            float neededPerUnit = static_cast<float>(input.amount);
            if (neededPerUnit > 0.0f) {
                minInputRate = std::min(minInputRate, inputRate / neededPerUnit);
            }
        }
        if (minInputRate < 1000.0f) {
            totalRate += minInputRate * static_cast<float>(recipe.outputAmount);
        }
    }

    visited.erase(goodId);
    return totalRate;
}

/// Fill `outRates` with this player's per-good production rate vector.
/// Reuses one `visited` set across all goods so recursion-cycle detection
/// is amortized; the set is cleared between top-level goods (each call to
/// computeRate erases on exit, leaving it empty for the next entry).
void fillPlayerRates(const aoc::game::GameState& gameState,
                     PlayerId player,
                     std::vector<float>& outRates) {
    const uint16_t count = goodCount();
    outRates.assign(count, 0.0f);
    std::unordered_set<uint16_t> visited;
    for (uint16_t g = 0; g < count; ++g) {
        outRates[g] = computeRate(gameState, player, g, visited);
    }
}

} // namespace

float playerProductionRate(const aoc::game::GameState& gameState,
                            PlayerId player,
                            uint16_t goodId) {
    // Public single-good wrapper. Pairwise comparisons use fillPlayerRates
    // (single batched pass per player) — see computeComparativeAdvantage.
    std::unordered_set<uint16_t> visited;
    return computeRate(gameState, player, goodId, visited);
}

std::vector<TradeRecommendation> computeComparativeAdvantage(
    const aoc::game::GameState& gameState,
    const Market& market,
    PlayerId playerA,
    PlayerId playerB) {
    std::vector<TradeRecommendation> recommendations;

    // Single batched fill per player. Each fill amortizes the recipe-DAG
    // walk over all goods, instead of restarting the recursion from scratch
    // 2 * goodCount() times. Audit Warning: pairwise was O(n_civs² × goods
    // × recipe_depth) per turn — caching the rate vectors per call collapses
    // it to O(goods × recipe_depth) per civ.
    uint16_t count = goodCount();
    std::vector<float> ratesA;
    std::vector<float> ratesB;
    fillPlayerRates(gameState, playerA, ratesA);
    fillPlayerRates(gameState, playerB, ratesB);

    float totalA = 0.0f;
    float totalB = 0.0f;
    for (uint16_t g = 0; g < count; ++g) {
        totalA += ratesA[g] * static_cast<float>(market.price(g));
        totalB += ratesB[g] * static_cast<float>(market.price(g));
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
