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
#include "aoc/map/MapGenerator.hpp"
#include "aoc/simulation/victory/VictoryCondition.hpp"

#include <atomic>
#include <cstdint>
#include <span>
#include <vector>

namespace aoc::ga {

class ThreadPool;

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
    /// Victory type that ended the sim (None if timed out without winner).
    aoc::sim::VictoryType victoryType = aoc::sim::VictoryType::None;
    /// Winning player ID (INVALID_PLAYER if no winner).
    aoc::PlayerId         winner      = aoc::INVALID_PLAYER;
    /// Whether the simulation completed successfully.
    bool valid = false;
};

/// Run a single headless simulation and return structured results.
/// Thread-safe: creates its own GameState, grid, and RNG from the given seed.
/// If stopFlag is non-null and becomes true, the sim exits early and
/// returns a result with valid=false.
///
/// `overrides` supplies per-player personality injections via a thread-local
/// override table. overrides[p] != nullptr installs that individual's genes
/// on Player p's civId for the duration of this simulation. nullptr entries
/// fall through to the hand-crafted civ-type personality.
///
/// An empty span runs with default personalities for all players (useful
/// for baseline runs). The RAII guard inside clears every installed
/// override on return, including early abort via stopFlag, so subsequent
/// sims on this thread start clean.
///
/// Assumes playerCount <= CIV_COUNT so distinct civIds are assigned to
/// each player; colliding civIds would share the same override slot.
[[nodiscard]] SimulationResult runSimulation(int32_t turns, int32_t playerCount,
                                              uint64_t seed,
                                              const std::atomic<bool>* stopFlag = nullptr,
                                              std::span<const Individual* const> overrides = {},
                                              aoc::map::MapType mapType = aoc::map::MapType::Realistic);

/// Evaluate fitness of one individual by running multiple games.
/// The individual's genes are used as Player 0's AI personality.
/// Fitness = average normalized EraVP score across games.
/// When config.turnsList / config.playersList are non-empty, game k uses
/// the k-th entry (cycling) from the respective list; otherwise the scalar
/// turnsPerGame / playerCount is used for every game.
[[nodiscard]] float evaluateFitness(const Individual& individual,
                                     const GAConfig& config,
                                     uint64_t baseSeed);

/// Evaluate fitness for an entire population.
///
/// When `pool` is non-null, work is flattened to game-level tasks
/// (pop × gamesPerEval) and submitted to the persistent pool. This gives
/// better core utilization than individual-level parallelism because long
/// 500-turn games can overlap with short 200-turn games across workers.
///
/// When `pool` is null, falls back to a serial loop (useful for debugging).
///
/// `hallOfFame` (optional) is sampled for opponent slots in Champion/Mixed
/// modes. May be nullptr or empty; in that case Champion mode falls back
/// to the current population's best-so-far genome.
void evaluatePopulation(std::vector<Individual>& population,
                         const GAConfig& config,
                         uint64_t baseSeed,
                         ThreadPool* pool,
                         const std::vector<Individual>* hallOfFame = nullptr);

} // namespace aoc::ga
