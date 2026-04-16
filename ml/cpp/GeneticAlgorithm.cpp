/**
 * @file GeneticAlgorithm.cpp
 * @brief GA operators: selection, crossover, mutation, population init.
 */

#include "GeneticAlgorithm.hpp"

#include <algorithm>
#include <cstdio>

namespace aoc::ga {

// ============================================================================
// Existing leader profiles (12 leaders, mirrors Python EXISTING_LEADERS)
// ============================================================================

static constexpr std::array<std::array<float, NUM_PARAMS>, 12> EXISTING_LEADERS = {{
    // Rome - Trajan
    {{1.3f, 1.8f, 1.0f, 1.0f, 1.2f, 1.0f, 0.5f, 0.3f, 0.8f, 0.6f,
      1.2f, 1.0f, 1.5f, 0.8f, 0.8f, 1.8f, 1.2f, 1.5f, 1.3f, 1.0f, 0.7f, 0.3f, 1.5f, 0.6f, 1.0f}},
    // Egypt - Cleopatra
    {{0.7f, 1.0f, 1.0f, 1.5f, 1.8f, 1.3f, 0.8f, 0.0f, 0.9f, 0.4f,
      0.7f, 1.8f, 1.0f, 1.3f, 1.0f, 1.0f, 0.6f, 0.8f, 1.5f, 1.8f, 1.2f, 0.5f, 2.0f, 0.3f, 1.3f}},
    // China - Qin Shi Huang
    {{0.8f, 1.2f, 1.5f, 1.3f, 1.2f, 0.7f, 0.5f, 0.2f, 1.0f, 0.8f,
      0.8f, 1.0f, 1.5f, 0.6f, 1.3f, 1.2f, 0.8f, 1.0f, 1.5f, 2.0f, 0.5f, 0.3f, 2.5f, 0.4f, 0.7f}},
    // Germany - Frederick
    {{1.5f, 1.3f, 1.3f, 0.8f, 1.5f, 0.8f, 0.3f, 0.5f, 0.9f, 0.7f,
      1.8f, 1.2f, 1.8f, 0.8f, 0.8f, 1.2f, 1.8f, 1.0f, 1.5f, 0.5f, 0.8f, 0.2f, 1.2f, 0.8f, 0.7f}},
    // Greece - Pericles
    {{0.6f, 0.8f, 1.6f, 1.8f, 0.9f, 1.5f, 0.7f, 0.0f, 1.0f, 0.3f,
      0.5f, 0.8f, 0.8f, 0.7f, 1.8f, 0.8f, 0.5f, 0.7f, 1.8f, 1.5f, 0.5f, 0.5f, 3.0f, 0.3f, 1.5f}},
    // England - Victoria
    {{1.2f, 1.5f, 1.2f, 1.2f, 1.7f, 1.2f, 0.5f, 0.3f, 0.7f, 0.5f,
      1.0f, 1.5f, 1.0f, 2.0f, 1.2f, 1.5f, 1.0f, 1.0f, 1.3f, 1.0f, 1.8f, 0.3f, 1.5f, 0.5f, 1.2f}},
    // Japan - Hojo
    {{1.6f, 0.9f, 1.3f, 1.5f, 1.0f, 0.7f, 1.3f, 0.4f, 1.0f, 0.9f,
      1.5f, 0.8f, 1.2f, 1.0f, 1.0f, 0.9f, 1.6f, 0.8f, 1.3f, 1.3f, 1.0f, 1.5f, 1.3f, 0.7f, 0.7f}},
    // Persia - Cyrus
    {{1.4f, 1.3f, 1.0f, 1.0f, 1.3f, 1.4f, 0.8f, 0.2f, 0.5f, 0.6f,
      1.3f, 1.5f, 1.0f, 0.8f, 0.8f, 1.3f, 1.4f, 1.0f, 1.2f, 0.8f, 0.8f, 0.5f, 1.0f, 0.4f, 1.5f}},
    // Aztec - Montezuma
    {{1.7f, 1.2f, 0.7f, 0.8f, 1.0f, 0.6f, 1.5f, 0.3f, 0.7f, 0.9f,
      1.8f, 0.7f, 0.8f, 0.5f, 0.5f, 1.2f, 2.0f, 1.0f, 0.8f, 0.5f, 0.5f, 1.8f, 1.0f, 0.8f, 0.5f}},
    // India - Gandhi
    {{0.2f, 0.7f, 1.3f, 1.3f, 1.0f, 1.8f, 1.6f, 0.0f, 1.0f, 0.2f,
      0.3f, 1.0f, 0.8f, 0.5f, 1.5f, 0.7f, 0.2f, 0.8f, 1.5f, 1.0f, 0.3f, 2.0f, 5.0f, 0.2f, 1.8f}},
    // Russia - Peter
    {{1.3f, 1.5f, 1.7f, 0.8f, 1.2f, 1.0f, 0.7f, 0.4f, 0.8f, 0.6f,
      1.2f, 1.0f, 1.5f, 0.8f, 1.8f, 1.5f, 1.2f, 1.0f, 1.5f, 0.8f, 0.8f, 0.5f, 1.5f, 0.5f, 1.0f}},
    // Brazil - Pedro
    {{0.5f, 1.0f, 1.0f, 1.8f, 1.2f, 1.5f, 0.8f, 0.0f, 1.0f, 0.1f,
      0.4f, 1.0f, 0.8f, 0.7f, 1.3f, 1.0f, 0.4f, 0.8f, 1.5f, 1.8f, 0.5f, 0.8f, 3.0f, 0.2f, 1.5f}},
}};

// Parameter names for output (same order as LeaderBehavior fields).
static constexpr const char* PARAM_NAMES[NUM_PARAMS] = {
    "militaryAggression", "expansionism", "scienceFocus", "cultureFocus",
    "economicFocus", "diplomaticOpenness", "religiousZeal", "nukeWillingness",
    "trustworthiness", "grudgeHolding",
    "techMilitary", "techEconomic", "techIndustrial", "techNaval", "techInformation",
    "prodSettlers", "prodMilitary", "prodBuilders", "prodBuildings", "prodWonders",
    "prodNaval", "prodReligious",
    "warDeclarationThreshold", "peaceAcceptanceThreshold", "allianceDesire",
};

void clampGenes(std::array<float, NUM_PARAMS>& genes, const ParamBounds& bounds) {
    for (int32_t i = 0; i < NUM_PARAMS; ++i) {
        if (genes[static_cast<std::size_t>(i)] < bounds.min[static_cast<std::size_t>(i)]) {
            genes[static_cast<std::size_t>(i)] = bounds.min[static_cast<std::size_t>(i)];
        }
        if (genes[static_cast<std::size_t>(i)] > bounds.max[static_cast<std::size_t>(i)]) {
            genes[static_cast<std::size_t>(i)] = bounds.max[static_cast<std::size_t>(i)];
        }
    }
}

std::vector<Individual> createInitialPopulation(int32_t popSize, std::mt19937& rng,
                                                 const ParamBounds& bounds) {
    std::vector<Individual> population;
    population.reserve(static_cast<std::size_t>(popSize));

    // Seed with existing leaders
    for (int32_t i = 0; i < 12 && static_cast<int32_t>(population.size()) < popSize; ++i) {
        Individual ind{};
        ind.genes = EXISTING_LEADERS[static_cast<std::size_t>(i)];
        population.push_back(ind);
    }

    // Fill remaining with mutations of existing leaders
    std::normal_distribution<float> mutDist(0.0f, 0.3f);
    std::uniform_int_distribution<int32_t> parentDist(0, 11);

    while (static_cast<int32_t>(population.size()) < popSize) {
        Individual ind{};
        ind.genes = EXISTING_LEADERS[static_cast<std::size_t>(parentDist(rng))];
        for (int32_t j = 0; j < NUM_PARAMS; ++j) {
            ind.genes[static_cast<std::size_t>(j)] += mutDist(rng);
        }
        clampGenes(ind.genes, bounds);
        population.push_back(ind);
    }

    return population;
}

Individual tournamentSelect(const std::vector<Individual>& population,
                             int32_t tournamentSize, std::mt19937& rng) {
    std::uniform_int_distribution<int32_t> dist(0, static_cast<int32_t>(population.size()) - 1);

    int32_t bestIdx = dist(rng);
    for (int32_t i = 1; i < tournamentSize; ++i) {
        int32_t candidate = dist(rng);
        if (population[static_cast<std::size_t>(candidate)].fitness >
            population[static_cast<std::size_t>(bestIdx)].fitness) {
            bestIdx = candidate;
        }
    }
    return population[static_cast<std::size_t>(bestIdx)];
}

Individual crossover(const Individual& parentA, const Individual& parentB,
                      std::mt19937& rng) {
    std::uniform_real_distribution<float> coinFlip(0.0f, 1.0f);
    Individual child{};
    for (int32_t i = 0; i < NUM_PARAMS; ++i) {
        std::size_t idx = static_cast<std::size_t>(i);
        child.genes[idx] = (coinFlip(rng) < 0.5f) ? parentA.genes[idx] : parentB.genes[idx];
    }
    return child;
}

Individual mutate(const Individual& individual, float mutationRate, float sigma,
                   float resetRate, const ParamBounds& bounds, std::mt19937& rng) {
    std::uniform_real_distribution<float> uniform(0.0f, 1.0f);
    std::normal_distribution<float> gaussian(0.0f, sigma);
    Individual result{};
    result.genes = individual.genes;

    for (int32_t i = 0; i < NUM_PARAMS; ++i) {
        if (uniform(rng) < mutationRate) {
            std::size_t idx = static_cast<std::size_t>(i);
            if (uniform(rng) < resetRate) {
                // Reset mutation: random value in valid range
                std::uniform_real_distribution<float> rangeDist(
                    bounds.min[idx], bounds.max[idx]);
                result.genes[idx] = rangeDist(rng);
            } else {
                // Gaussian perturbation
                result.genes[idx] += gaussian(rng);
            }
        }
    }

    clampGenes(result.genes, bounds);
    return result;
}

DifficultyTiers extractTiers(const std::vector<Individual>& sortedPopulation) {
    DifficultyTiers tiers{};
    if (sortedPopulation.empty()) {
        return tiers;
    }
    tiers.hard   = sortedPopulation.front();
    tiers.easy   = sortedPopulation.back();
    tiers.medium = sortedPopulation[sortedPopulation.size() / 2];
    return tiers;
}

void printAsCppInitializer(const Individual& individual, const char* label) {
    std::fprintf(stderr, "\n--- %s (fitness=%.4f) ---\n", label,
                 static_cast<double>(individual.fitness));
    std::fprintf(stderr, "  // C++ LeaderBehavior initializer:\n  {");
    for (int32_t i = 0; i < NUM_PARAMS; ++i) {
        if (i > 0 && i % 5 == 0) {
            std::fprintf(stderr, "\n   ");
        }
        std::fprintf(stderr, "%.2ff", static_cast<double>(individual.genes[static_cast<std::size_t>(i)]));
        if (i < NUM_PARAMS - 1) {
            std::fprintf(stderr, ", ");
        }
    }
    std::fprintf(stderr, "}\n");

    // Also print named parameters
    for (int32_t i = 0; i < NUM_PARAMS; ++i) {
        std::fprintf(stderr, "  %-30s = %.3f\n", PARAM_NAMES[i],
                     static_cast<double>(individual.genes[static_cast<std::size_t>(i)]));
    }
}

} // namespace aoc::ga
