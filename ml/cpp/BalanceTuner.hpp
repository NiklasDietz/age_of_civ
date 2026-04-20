#pragma once

/**
 * @file BalanceTuner.hpp
 * @brief GA that tunes BalanceParams (game balance constants) for *game
 *        health*, not single-player advantage.
 *
 * The AI-gene GA in GeneticAlgorithm.hpp optimises a LeaderBehavior so one
 * subject beats fixed/co-evolving opponents. This tuner is different: the
 * "individual" is a BalanceGenome (11 scalars controlling loyalty,
 * victory-condition thresholds, space-race pacing, etc.), and fitness is a
 * population-wide metric over an N-game batch:
 *
 *   - victory-type entropy   (Shannon H normalised to [0,1])
 *   - game-length target     (rewards finishing at ~60% of turn budget)
 *   - final-score Gini band  (rewards moderate inequality, not runaway)
 *
 * The 11 constexpr sites in the sim read from aoc::balance::params(), so
 * installing a new BalanceParams and re-running the sim is all that's
 * needed per individual. No rebuild.
 *
 * IMPORTANT: the global singleton forbids evaluating two *different*
 * balance genomes in parallel on the same process -- they would stomp on
 * each other's params. We run individuals serially and parallelise only
 * the N games within one individual's fitness eval.
 */

#include "FitnessEvaluator.hpp"
#include "ThreadPool.hpp"
#include "aoc/balance/BalanceParams.hpp"
#include "aoc/map/MapGenerator.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <random>
#include <vector>

namespace aoc::ga {

/// Per-individual fitness metrics. Kept separate from the scalar fitness
/// so the GA log can print which health axis each genome nailed/missed.
struct BalanceHealth {
    float entropy       = 0.0f;  ///< Victory-type entropy (0..1)
    float lengthScore   = 0.0f;  ///< Closeness of avg length to target (0..1)
    float giniScore     = 0.0f;  ///< Reward for moderate Gini band (0..1)
    float decisiveShare = 0.0f;  ///< Fraction of games that ended by a non-Score win
    float avgTurns      = 0.0f;  ///< Avg turns to end across the batch
};

struct BalanceIndividual {
    aoc::balance::BalanceGenome genome{};
    float          fitness     = 0.0f;
    BalanceHealth  health{};
    int32_t        gamesPlayed = 0;
};

/// Configuration for the balance tuner. Mirrors GAConfig but keyed to
/// game-health search rather than single-player fitness.
struct BalanceGAConfig {
    int32_t populationSize = 32;
    int32_t generations    = 40;
    int32_t gamesPerEval   = 4;
    int32_t turnsPerGame   = 400;
    int32_t playerCount    = 8;
    int32_t elitism        = 2;
    int32_t tournamentSize = 3;
    float   mutationRate   = 0.25f;
    float   mutationSigma  = 0.12f;
    float   resetRate      = 0.05f;
    int32_t threadCount    = 0;

    /// Optional per-game turns/players/maps cycling (same semantics as
    /// the AI-GA). Healthier balance tunes tend to emerge when mixed.
    std::vector<int32_t>            turnsList;
    std::vector<int32_t>            playersList;
    std::vector<aoc::map::MapType>  mapsList;

    /// Fitness weights (sum need not be 1.0 -- only ranking matters).
    float wEntropy   = 0.50f;
    float wLength   = 0.25f;
    float wGini     = 0.15f;
    float wDecisive = 0.10f;

    /// Target fraction of turn budget we want games to end at (0..1).
    /// 0.0 = end on turn 1 (bad); 1.0 = time out (bad); 0.6 = healthy.
    float lengthTargetFrac = 0.6f;

    const std::atomic<bool>* stopFlag = nullptr;
};

/// Evaluate one balance genome over N games and compute fitness + health.
/// Installs the genome into the global BalanceParams singleton for the
/// duration of the call, then restores the prior params before return.
/// Not thread-safe across balance individuals; callers must serialise.
void evaluateBalanceIndividual(BalanceIndividual& individual,
                                const BalanceGAConfig& config,
                                uint64_t baseSeed,
                                ThreadPool* pool);

/// Run the full balance GA and return the final population (sorted
/// descending by fitness). Writes progress to stderr.
[[nodiscard]] std::vector<BalanceIndividual>
runBalanceGA(const BalanceGAConfig& config,
             uint64_t masterSeed,
             ThreadPool* pool);

/// Dump a human-readable summary of the top-N balance genomes to path.
void saveBalanceSummary(const std::vector<BalanceIndividual>& sortedPop,
                         const char* path,
                         int32_t topN = 3);

} // namespace aoc::ga
