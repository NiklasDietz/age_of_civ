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
#include "aoc/map/MapGenerator.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <random>
#include <string_view>
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
          0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.0f,
          0.5f, 0.0f, 0.5f, 0.5f}},
        // max
        {{2.5f, 2.5f, 2.5f, 2.5f, 2.5f, 2.0f, 2.0f, 1.0f, 1.0f, 1.0f,
          2.0f, 2.0f, 2.0f, 2.0f, 2.0f,
          2.5f, 2.5f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f,
          5.0f, 1.0f, 2.0f,
          2.5f, 2.5f, 2.5f, 2.5f, 2.5f, 2.5f, 2.0f,
          3.0f, 2.5f, 2.5f, 3.0f}}
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

/// How opponents (non-evaluated players) are chosen each game.
enum class OpponentMode : uint8_t {
    /// Opponents use hand-crafted civ-type personalities (no override).
    /// Legacy behavior. Deterministic per seed.
    Fixed    = 0,
    /// Opponents are random distinct individuals drawn from the current
    /// population. True co-evolution: each generation's winners must beat
    /// the generation's other genomes.
    CoEvolve = 1,
    /// Opponents sampled from the Hall of Fame (top-K best-ever so far).
    /// Forces the population to beat historically strong genomes. HoF is
    /// seeded from the hand-crafted LEADER_PERSONALITIES at startup so this
    /// mode is usable from generation 0.
    Champion = 2,
    /// Per-slot random mix of Fixed/CoEvolve/Champion. Exposes each
    /// individual to heterogeneous opponents in a single game.
    Mixed    = 3,
};

/// Parse opponent-mode name (case-insensitive). Returns false on unknown.
[[nodiscard]] bool parseOpponentMode(std::string_view s, OpponentMode& out);

/// Human-readable name for logging.
[[nodiscard]] const char* opponentModeName(OpponentMode mode);

/// Parse map-type name (case-insensitive). Returns false on unknown.
/// Accepts: continents, pangaea, archipelago, fractal, realistic.
[[nodiscard]] bool parseMapType(std::string_view s, aoc::map::MapType& out);

/// Human-readable name for logging.
[[nodiscard]] const char* mapTypeName(aoc::map::MapType type);

/// GA configuration.
struct GAConfig {
    int32_t      populationSize  = 20;
    int32_t      generations     = 50;
    int32_t      gamesPerEval    = 3;
    int32_t      turnsPerGame    = 200;
    int32_t      playerCount     = 8;
    int32_t      elitism         = 2;
    int32_t      tournamentSize  = 3;
    float        mutationRate    = 0.2f;
    float        mutationSigma   = 0.15f;
    float        resetRate       = 0.1f; ///< Probability of reset mutation vs Gaussian
    int32_t      threadCount     = 0;    ///< 0 = auto-detect
    OpponentMode opponentMode    = OpponentMode::Fixed;
    int32_t      hallOfFameSize  = 8;    ///< Top-K best-ever preserved for Champion mode

    /// Optional per-game turn counts. When non-empty, game k uses
    /// turnsList[k % turnsList.size()] instead of turnsPerGame. Enables
    /// mixed-size training (short + long games in one fitness eval).
    std::vector<int32_t> turnsList;

    /// Optional per-game player counts. When non-empty, game k uses
    /// playersList[k % playersList.size()] instead of playerCount.
    std::vector<int32_t> playersList;

    /// Optional per-game map types. When non-empty, game k uses
    /// mapsList[k % mapsList.size()] instead of the default Realistic.
    /// Exposes each genome to varied geography (Continents vs Archipelago
    /// vs Pangaea etc.) so evolved weights must generalize across naval,
    /// land-war and expansion-friendly conditions instead of overfitting
    /// one map family.
    std::vector<aoc::map::MapType> mapsList;

    /// External stop flag for fast SIGINT/SIGTERM abort. When non-null and
    /// true, runSimulation / evaluateFitness / evaluatePopulation exit at
    /// the next safe checkpoint (per-turn, per-game, per-individual).
    const std::atomic<bool>* stopFlag = nullptr;

    /// Balance-winrate mode: rewards individuals whose wins happen via
    /// currently-rare victory types. Post-hoc pass over each generation
    /// builds a histogram of (winner-subject) victory types across all
    /// pop × games evaluations, then adds a per-game bonus to each win
    /// proportional to how under-represented that type is. Pushes the
    /// GA away from dominance-spam convergence toward a population that
    /// wins through multiple mechanics.
    bool  balanceWinrate = false;

    /// Max per-game bonus applied on the rarest win type (added to the
    /// outcome score for that game). Rare = currently-least-observed
    /// victory type in the generation's win histogram. Dominant types
    /// get bonus ~0.
    float balanceBonus   = 0.8f;
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
/// When `seedLeader` is in [0, 11], ALL seeds (and mutation parents) are that
/// single leader -- used to tune values for one specific archetype. Otherwise
/// seeds rotate through all 12 leaders.
std::vector<Individual> createInitialPopulation(int32_t popSize, std::mt19937& rng,
                                                 const ParamBounds& bounds,
                                                 int32_t seedLeader = -1);

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
