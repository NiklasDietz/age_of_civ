#pragma once

/**
 * @file GeneticAlgorithm.hpp
 * @brief Genetic algorithm for optimizing LeaderBehavior weights.
 *
 * Evolves a population of 25-float genomes representing AI personality
 * parameters. Uses tournament selection, uniform crossover, and Gaussian
 * mutation with occasional resets. Elitism preserves top N individuals.
 */

#include "aoc/simulation/ai/LeaderPersonality.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <random>
#include <vector>

namespace aoc::ga {

constexpr int32_t NUM_PARAMS = aoc::sim::LeaderBehavior::PARAM_COUNT;

/// Valid range for each parameter (mirrors Python GA's PARAM_MIN/PARAM_MAX).
struct ParamBounds {
    std::array<float, NUM_PARAMS> min;
    std::array<float, NUM_PARAMS> max;
};

/// Default parameter bounds matching the Python evolve_utility.py.
[[nodiscard]] constexpr ParamBounds defaultBounds() {
    return ParamBounds{
        // min
        {{0.1f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.0f, 0.0f, 0.3f, 0.1f,
          0.3f, 0.3f, 0.3f, 0.3f, 0.3f,
          0.3f, 0.3f, 0.3f, 0.3f, 0.0f, 0.0f, 0.0f,
          0.5f, 0.1f, 0.3f,
          0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.0f}},
        // max
        {{2.5f, 2.5f, 2.5f, 2.5f, 2.5f, 2.0f, 2.0f, 1.0f, 1.0f, 1.0f,
          2.0f, 2.0f, 2.0f, 2.0f, 2.0f,
          2.5f, 2.5f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f,
          5.0f, 1.0f, 2.0f,
          2.5f, 2.5f, 2.5f, 2.5f, 2.5f, 2.5f, 2.0f}}
    };
}

/// One candidate AI personality.
struct Individual {
    std::array<float, NUM_PARAMS> genes{};
    float fitness = 0.0f;
    int32_t gamesPlayed = 0;

    /// Convert to LeaderBehavior for use in simulation.
    [[nodiscard]] aoc::sim::LeaderBehavior toBehavior() const {
        aoc::sim::LeaderBehavior behavior{};
        behavior.fromArray(this->genes.data());
        return behavior;
    }

    /// Initialize from a LeaderBehavior.
    void fromBehavior(const aoc::sim::LeaderBehavior& behavior) {
        behavior.toArray(this->genes.data());
    }
};

/// GA configuration.
struct GAConfig {
    int32_t populationSize  = 20;
    int32_t generations     = 50;
    int32_t gamesPerEval    = 3;
    int32_t turnsPerGame    = 200;
    int32_t playerCount     = 8;
    int32_t elitism         = 2;
    int32_t tournamentSize  = 3;
    float   mutationRate    = 0.2f;
    float   mutationSigma   = 0.15f;
    float   resetRate       = 0.1f;   ///< Probability of reset mutation vs Gaussian
    int32_t threadCount     = 0;      ///< 0 = auto-detect

    /// Optional per-game turn counts. When non-empty, game k uses
    /// turnsList[k % turnsList.size()] instead of turnsPerGame. Enables
    /// mixed-size training (short + long games in one fitness eval).
    std::vector<int32_t> turnsList;

    /// Optional per-game player counts. When non-empty, game k uses
    /// playersList[k % playersList.size()] instead of playerCount.
    std::vector<int32_t> playersList;

    /// External stop flag for fast SIGINT/SIGTERM abort. When non-null and
    /// true, runSimulation / evaluateFitness / evaluatePopulation exit at
    /// the next safe checkpoint (per-turn, per-game, per-individual).
    const std::atomic<bool>* stopFlag = nullptr;
};

/// Difficulty tier results.
struct DifficultyTiers {
    Individual hard;
    Individual medium;
    Individual easy;
};

/// Clamp genes to valid parameter bounds.
void clampGenes(std::array<float, NUM_PARAMS>& genes, const ParamBounds& bounds);

/// Create initial population seeded from existing leader profiles + mutations.
std::vector<Individual> createInitialPopulation(int32_t popSize, std::mt19937& rng,
                                                 const ParamBounds& bounds);

/// Tournament selection: pick tournamentSize random individuals, return best.
Individual tournamentSelect(const std::vector<Individual>& population,
                             int32_t tournamentSize, std::mt19937& rng);

/// Uniform crossover: for each gene, randomly pick from parent A or B.
Individual crossover(const Individual& parentA, const Individual& parentB,
                      std::mt19937& rng);

/// Gaussian mutation with occasional reset.
Individual mutate(const Individual& individual, float mutationRate, float sigma,
                   float resetRate, const ParamBounds& bounds, std::mt19937& rng);

/// Extract Easy/Medium/Hard tiers from a sorted population.
DifficultyTiers extractTiers(const std::vector<Individual>& sortedPopulation);

/// Print an individual as a C++ LeaderBehavior initializer.
void printAsCppInitializer(const Individual& individual, const char* label);

} // namespace aoc::ga
