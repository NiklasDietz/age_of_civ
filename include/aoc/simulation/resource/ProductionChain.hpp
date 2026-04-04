#pragma once

/**
 * @file ProductionChain.hpp
 * @brief DAG representation of resource processing chains with topological sort.
 *
 * The production chain is a directed acyclic graph where nodes are goods
 * and edges are recipe dependencies (input -> output). Each turn, the
 * simulation processes recipes in topological order so that raw materials
 * are available before the recipes that consume them execute.
 */

#include "aoc/simulation/resource/ResourceTypes.hpp"

#include <cstdint>
#include <vector>

namespace aoc::sim {

class ProductionChain {
public:
    /**
     * @brief Build the DAG from all registered recipes.
     *
     * Must be called once at game startup (or when recipes change).
     * Computes the topological execution order.
     */
    void build();

    /**
     * @brief Get recipes in topological order (raw inputs first, advanced outputs last).
     *
     * Iterating this list front-to-back guarantees that when a recipe executes,
     * all its input goods have already been produced by earlier recipes.
     */
    [[nodiscard]] const std::vector<const ProductionRecipe*>& executionOrder() const {
        return this->m_executionOrder;
    }

    /**
     * @brief Get all recipes that produce a given good.
     */
    [[nodiscard]] std::vector<const ProductionRecipe*> recipesProducing(uint16_t goodId) const;

    /**
     * @brief Get all recipes that consume a given good as input.
     */
    [[nodiscard]] std::vector<const ProductionRecipe*> recipesConsuming(uint16_t goodId) const;

private:
    std::vector<const ProductionRecipe*> m_executionOrder;
};

} // namespace aoc::sim
