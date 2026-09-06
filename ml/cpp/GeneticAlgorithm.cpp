/**
 * @file GeneticAlgorithm.cpp
 * @brief GA operators: selection, crossover, mutation, population init.
 */

#include "GeneticAlgorithm.hpp"

#include "aoc/simulation/ai/LeaderPersonality.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

namespace aoc::ga {

bool parseOpponentMode(std::string_view s, OpponentMode& out) {
    std::string lower;
    lower.reserve(s.size());
    for (char c : s) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (lower == "fixed")    { out = OpponentMode::Fixed;    return true; }
    if (lower == "coevolve" || lower == "co-evolve") {
        out = OpponentMode::CoEvolve; return true;
    }
    if (lower == "champion" || lower == "hof") {
        out = OpponentMode::Champion; return true;
    }
    if (lower == "mixed")    { out = OpponentMode::Mixed;    return true; }
    return false;
}

const char* opponentModeName(OpponentMode mode) {
    switch (mode) {
        case OpponentMode::Fixed:    return "fixed";
        case OpponentMode::CoEvolve: return "coevolve";
        case OpponentMode::Champion: return "champion";
        case OpponentMode::Mixed:    return "mixed";
    }
    return "?";
}

bool parseMapType(std::string_view s, aoc::map::MapType& out) {
    // 2026-05-03: only Continents supported. Other strings remap to Continents
    // so existing GA configs (maps:[islands,fractal]) keep parsing.
    std::string lower;
    lower.reserve(s.size());
    for (char c : s) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (lower == "continents") { out = aoc::map::MapType::Continents; return true; }
    if (lower.empty()) { return false; }
    std::fprintf(stderr,
                 "[Config] map type '%.*s' is not available; remapped to "
                 "continents (only Continents is supported since 2026-05-03).\n",
                 static_cast<int>(s.size()), s.data());
    out = aoc::map::MapType::Continents;
    return true;
}

const char* mapTypeName(aoc::map::MapType type) {
    switch (type) {
        case aoc::map::MapType::Continents: return "continents";
    }
    return "?";
}

// ============================================================================
// Existing leader profiles: the twelve archetypes, read from the game
// ============================================================================

/// The GA used to keep its own copy of the twelve archetypes here. It drifted:
/// by 2026-09-06 the shipped LEADER_PERSONALITIES and this table disagreed on
/// 148 of their 384 shared gene slots, every one of the twelve differing in ten
/// to fifteen genes. That meant `--seed-leader N` tuned a leader the game does
/// not have, and Champion mode measured candidates against opponents that do
/// not exist. Reading the shipped table makes the drift impossible rather than
/// merely fixed.
[[nodiscard]] static std::array<std::array<float, NUM_PARAMS>, 12> loadExistingLeaders() {
    std::array<std::array<float, NUM_PARAMS>, 12> out{};
    for (std::size_t i = 0; i < out.size(); ++i) {
        aoc::sim::LEADER_PERSONALITIES[i].behavior.toArray(out[i].data());
    }
    return out;
}

static const std::array<std::array<float, NUM_PARAMS>, 12> EXISTING_LEADERS =
    loadExistingLeaders();

static_assert(aoc::sim::LEADER_PERSONALITY_COUNT >= 12,
              "the GA seeds from the first twelve shipped leaders");

// PARAM_NAMES now lives in GeneticAlgorithm.hpp (single shared definition).

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
                                                 const ParamBounds& bounds,
                                                 int32_t seedLeader) {
    std::vector<Individual> population;
    population.reserve(static_cast<std::size_t>(popSize));

    const bool singleLeader = (seedLeader >= 0 && seedLeader < 12);
    std::normal_distribution<float> mutDist(0.0f, 0.3f);
    std::uniform_int_distribution<int32_t> parentDist(0, 11);

    if (singleLeader) {
        // Seed slot 0 with the archetype (clamped to bounds) so the target
        // flavour is always in the gene pool. Clamping matters: some shipped
        // archetypes sit just outside defaultBounds() (e.g. Gandhi's
        // prodMilitary 0.2 < the 0.3 floor), and an out-of-bounds seed would
        // otherwise poison the initial population. Fill remainder with mutations.
        Individual seed{};
        seed.genes = EXISTING_LEADERS[static_cast<std::size_t>(seedLeader)];
        clampGenes(seed.genes, bounds);
        population.push_back(seed);
        while (static_cast<int32_t>(population.size()) < popSize) {
            Individual ind{};
            ind.genes = EXISTING_LEADERS[static_cast<std::size_t>(seedLeader)];
            for (int32_t j = 0; j < NUM_PARAMS; ++j) {
                ind.genes[static_cast<std::size_t>(j)] += mutDist(rng);
            }
            clampGenes(ind.genes, bounds);
            population.push_back(ind);
        }
        return population;
    }

    // Default: rotate through all 12 hand-crafted leaders (clamped to bounds,
    // see note above), then mutations.
    for (int32_t i = 0; i < 12 && static_cast<int32_t>(population.size()) < popSize; ++i) {
        Individual ind{};
        ind.genes = EXISTING_LEADERS[static_cast<std::size_t>(i)];
        clampGenes(ind.genes, bounds);
        population.push_back(ind);
    }

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
