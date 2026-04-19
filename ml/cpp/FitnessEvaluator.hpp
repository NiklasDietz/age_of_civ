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

#include <atomic>
#include <cstdint>
#include <vector>

namespace aoc::ga {

/// Result of a single simulation run.
struct SimulationResult {
    /// Per-player EraVP scores (indexed by player ID).
    std::vector<int32_t> eraVP;
    /// Per-player composite CSI scores.
    std::vector<float> compositeCSI;
    /// Per-player final treasury (gold).
    std::vector<int32_t> treasury;
    /// Per-player final city count.
    std::vector<int32_t> cityCount;
    /// Per-player peak city count reached during the game (for survival metric).
    std::vector<int32_t> peakCityCount;
    /// Per-player final population sum.
    std::vector<int32_t> population;
    /// Per-player final GDP.
    std::vector<float> gdp;
    /// Per-player final average happiness across cities.
    std::vector<float> avgHappiness;
    /// Per-player final total income per turn.
    std::vector<float> totalIncome;
    /// Per-player final total expense per turn.
    std::vector<float> totalExpense;
    /// Whether the simulation completed successfully.
    bool valid = false;
};

/// Run a single headless simulation and return structured results.
/// Thread-safe: creates its own GameState, grid, and RNG from the given seed.
/// If stopFlag is non-null and becomes true, the sim exits early and
/// returns a result with valid=false.
///
/// If `individual` is non-null, its genes are installed as Player 0's AI
/// personality for the duration of this simulation via a thread-local
/// override. Other players use the default civ-type personalities. This is
/// how GA fitness evaluation actually exercises evolved genes.
[[nodiscard]] SimulationResult runSimulation(int32_t turns, int32_t playerCount,
                                              uint64_t seed,
                                              const std::atomic<bool>* stopFlag = nullptr,
                                              const Individual* individual = nullptr);

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
