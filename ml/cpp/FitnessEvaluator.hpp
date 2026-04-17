#pragma once

/**
 * @file FitnessEvaluator.hpp
 * @brief Evaluates GA individuals by running embedded headless simulations.
 *
 * Each simulation runs in its own thread with its own GameState and RNG,
 * eliminating all shared mutable state. Results are returned as structured
 * data (per-player EraVP scores), avoiding CSV serialization overhead.
 */

#include "GeneticAlgorithm.hpp"

#include <cstdint>
#include <vector>

namespace aoc::ga {

/// Result of a single simulation run.
struct SimulationResult {
    /// Per-player EraVP scores (indexed by player ID).
    std::vector<int32_t> eraVP;
    /// Per-player composite CSI scores.
    std::vector<float> compositeCSI;
    /// Whether the simulation completed successfully.
    bool valid = false;
};

/// Run a single headless simulation and return structured results.
/// Thread-safe: creates its own GameState, grid, and RNG from the given seed.
[[nodiscard]] SimulationResult runSimulation(int32_t turns, int32_t playerCount,
                                              uint64_t seed);

/// Evaluate fitness of one individual by running multiple games.
/// The individual's genes are used as Player 0's AI personality.
/// Fitness = average normalized EraVP score across games.
/// When config.turnsList / config.playersList are non-empty, game k uses
/// the k-th entry (cycling) from the respective list; otherwise the scalar
/// turnsPerGame / playerCount is used for every game.
[[nodiscard]] float evaluateFitness(const Individual& individual,
                                     const GAConfig& config,
                                     uint64_t baseSeed);

/// Evaluate fitness for an entire population using a thread pool.
/// Updates each individual's fitness and gamesPlayed in-place.
void evaluatePopulation(std::vector<Individual>& population,
                         const GAConfig& config,
                         uint64_t baseSeed);

} // namespace aoc::ga
