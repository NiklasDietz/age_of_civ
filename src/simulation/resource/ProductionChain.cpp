/**
 * @file ProductionChain.cpp
 * @brief Topological sort of production recipes.
 */

#include "aoc/simulation/resource/ProductionChain.hpp"

#include <algorithm>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace aoc::sim {

void ProductionChain::build() {
    const std::vector<ProductionRecipe>& recipes = allRecipes();
    this->m_executionOrder.clear();

    if (recipes.empty()) {
        return;
    }

    // Build dependency graph: for each recipe, which recipes must execute first?
    // A recipe R depends on recipe S if S produces a good that R consumes.
    std::unordered_map<uint16_t, std::vector<std::size_t>> producerMap;
    for (std::size_t i = 0; i < recipes.size(); ++i) {
        producerMap[recipes[i].outputGoodId].push_back(i);
    }

    // Adjacency: recipeIndex -> set of recipe indices that depend on it
    std::vector<std::vector<std::size_t>> adjacency(recipes.size());
    std::vector<int32_t> inDegree(recipes.size(), 0);

    for (std::size_t i = 0; i < recipes.size(); ++i) {
        for (const RecipeInput& input : recipes[i].inputs) {
            auto it = producerMap.find(input.goodId);
            if (it != producerMap.end()) {
                for (std::size_t producerIdx : it->second) {
                    // producerIdx must execute before i
                    adjacency[producerIdx].push_back(i);
                    ++inDegree[i];
                }
            }
        }
    }

    // Kahn's algorithm
    std::queue<std::size_t> ready;
    for (std::size_t i = 0; i < recipes.size(); ++i) {
        if (inDegree[i] == 0) {
            ready.push(i);
        }
    }

    this->m_executionOrder.reserve(recipes.size());
    while (!ready.empty()) {
        std::size_t current = ready.front();
        ready.pop();
        this->m_executionOrder.push_back(&recipes[current]);

        for (std::size_t neighbor : adjacency[current]) {
            --inDegree[neighbor];
            if (inDegree[neighbor] == 0) {
                ready.push(neighbor);
            }
        }
    }
    // If m_executionOrder.size() < recipes.size(), there's a cycle (should not happen).
}

std::vector<const ProductionRecipe*> ProductionChain::recipesProducing(uint16_t goodId) const {
    std::vector<const ProductionRecipe*> result;
    for (const ProductionRecipe& recipe : allRecipes()) {
        if (recipe.outputGoodId == goodId) {
            result.push_back(&recipe);
        }
    }
    return result;
}

std::vector<const ProductionRecipe*> ProductionChain::recipesConsuming(uint16_t goodId) const {
    std::vector<const ProductionRecipe*> result;
    for (const ProductionRecipe& recipe : allRecipes()) {
        for (const RecipeInput& input : recipe.inputs) {
            if (input.goodId == goodId) {
                result.push_back(&recipe);
                break;
            }
        }
    }
    return result;
}

} // namespace aoc::sim
