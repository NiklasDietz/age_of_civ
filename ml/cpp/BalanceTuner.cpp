/**
 * @file BalanceTuner.cpp
 * @brief Game-health GA over BalanceParams.  See BalanceTuner.hpp for design.
 */

#include "BalanceTuner.hpp"
#include "FitnessEvaluator.hpp"
#include "GeneticAlgorithm.hpp"
#include "ThreadPool.hpp"

#include "aoc/balance/BalanceParams.hpp"
#include "aoc/simulation/victory/VictoryCondition.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <future>
#include <numeric>
#include <random>
#include <vector>

namespace aoc::ga {

namespace {

constexpr int32_t NUM_BALANCE_GENES = aoc::balance::BALANCE_PARAM_COUNT;

/// Gini coefficient on a non-negative vector. Returns 0 for empty/all-zero.
[[nodiscard]] float gini(const std::vector<float>& xs) {
    if (xs.empty()) { return 0.0f; }
    std::vector<float> s = xs;
    for (float& v : s) { if (v < 0.0f) { v = 0.0f; } }
    std::sort(s.begin(), s.end());
    double sum = 0.0;
    for (float v : s) { sum += static_cast<double>(v); }
    if (sum <= 0.0) { return 0.0f; }
    const double n = static_cast<double>(s.size());
    double cum = 0.0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        cum += static_cast<double>(i + 1) * static_cast<double>(s[i]);
    }
    const double g = (2.0 * cum) / (n * sum) - (n + 1.0) / n;
    return static_cast<float>(std::clamp(g, 0.0, 1.0));
}

/// Triangle reward peaking at `target` with value 1, falling linearly to 0
/// at the clamp endpoints. Useful for "value-close-to-X" scoring.
[[nodiscard]] float triangleReward(float value, float target,
                                    float lowBound, float highBound) {
    if (value <= lowBound || value >= highBound) { return 0.0f; }
    if (value < target) {
        return (value - lowBound) / std::max(1e-6f, (target - lowBound));
    }
    return 1.0f - (value - target) / std::max(1e-6f, (highBound - target));
}

/// Normalised Shannon entropy over an int histogram, scaled by log(K) where
/// K is the number of buckets observed. Returns [0,1]: 0 when all games
/// share one outcome, 1 when uniformly distributed across multiple buckets.
[[nodiscard]] float normalisedEntropy(const std::array<int32_t, 8>& hist) {
    int32_t total = 0;
    int32_t occupied = 0;
    for (int32_t c : hist) {
        total += c;
        if (c > 0) { ++occupied; }
    }
    if (total <= 0 || occupied <= 1) { return 0.0f; }
    double h = 0.0;
    for (int32_t c : hist) {
        if (c == 0) { continue; }
        const double p = static_cast<double>(c) / static_cast<double>(total);
        h -= p * std::log(p);
    }
    const double maxH = std::log(static_cast<double>(occupied));
    return static_cast<float>(maxH > 0.0 ? h / maxH : 0.0);
}

/// Clamp a balance genome to the per-knob bounds in-place.
void clampBalanceGenes(std::array<float, NUM_BALANCE_GENES>& g,
                       const aoc::balance::BalanceBounds& b) {
    for (int32_t i = 0; i < NUM_BALANCE_GENES; ++i) {
        const std::size_t idx = static_cast<std::size_t>(i);
        g[idx] = std::clamp(g[idx], b.min[idx], b.max[idx]);
    }
}

/// Initial population: one individual at current defaults, the rest
/// uniformly sampled within bounds. Seeding the defaults guarantees a
/// fitness floor equal to the game we ship today.
std::vector<BalanceIndividual>
createInitialBalancePopulation(int32_t popSize, std::mt19937& rng,
                                const aoc::balance::BalanceBounds& bounds) {
    std::vector<BalanceIndividual> pop;
    pop.reserve(static_cast<std::size_t>(popSize));

    // Slot 0: current defaults.  Seeds the GA near known-working values.
    {
        BalanceIndividual seed{};
        seed.genome.fromParams(aoc::balance::BalanceParams{});
        pop.push_back(seed);
    }

    for (int32_t i = 1; i < popSize; ++i) {
        BalanceIndividual ind{};
        for (int32_t g = 0; g < NUM_BALANCE_GENES; ++g) {
            const std::size_t idx = static_cast<std::size_t>(g);
            std::uniform_real_distribution<float> U(bounds.min[idx], bounds.max[idx]);
            ind.genome.g[idx] = U(rng);
        }
        pop.push_back(ind);
    }
    return pop;
}

BalanceIndividual balanceTournamentSelect(const std::vector<BalanceIndividual>& pop,
                                          int32_t k, std::mt19937& rng) {
    std::uniform_int_distribution<std::size_t> U(0, pop.size() - 1);
    BalanceIndividual best = pop[U(rng)];
    for (int32_t i = 1; i < k; ++i) {
        const BalanceIndividual& cand = pop[U(rng)];
        if (cand.fitness > best.fitness) { best = cand; }
    }
    return best;
}

BalanceIndividual balanceCrossover(const BalanceIndividual& a,
                                    const BalanceIndividual& b,
                                    std::mt19937& rng) {
    BalanceIndividual child{};
    std::uniform_int_distribution<int32_t> coin(0, 1);
    for (int32_t i = 0; i < NUM_BALANCE_GENES; ++i) {
        const std::size_t idx = static_cast<std::size_t>(i);
        child.genome.g[idx] = coin(rng) ? a.genome.g[idx] : b.genome.g[idx];
    }
    return child;
}

BalanceIndividual balanceMutate(BalanceIndividual ind,
                                 const aoc::balance::BalanceBounds& bounds,
                                 float mutationRate, float sigma, float resetRate,
                                 std::mt19937& rng) {
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    for (int32_t i = 0; i < NUM_BALANCE_GENES; ++i) {
        const std::size_t idx = static_cast<std::size_t>(i);
        if (U(rng) >= mutationRate) { continue; }
        if (U(rng) < resetRate) {
            std::uniform_real_distribution<float> R(bounds.min[idx], bounds.max[idx]);
            ind.genome.g[idx] = R(rng);
        } else {
            const float range = bounds.max[idx] - bounds.min[idx];
            std::normal_distribution<float> N(0.0f, sigma * range);
            ind.genome.g[idx] += N(rng);
        }
    }
    clampBalanceGenes(ind.genome.g, bounds);
    return ind;
}

struct GameSpec {
    int32_t             turns;
    int32_t             players;
    aoc::map::MapType   map;
    uint64_t            seed;
};

/// Pick game parameters for game index k, cycling any per-game lists.
GameSpec pickGameSpec(int32_t k, const BalanceGAConfig& cfg, uint64_t baseSeed) {
    GameSpec s{};
    s.turns   = cfg.turnsList.empty() ? cfg.turnsPerGame
              : cfg.turnsList[static_cast<std::size_t>(k) % cfg.turnsList.size()];
    s.players = cfg.playersList.empty() ? cfg.playerCount
              : cfg.playersList[static_cast<std::size_t>(k) % cfg.playersList.size()];
    s.map     = cfg.mapsList.empty() ? aoc::map::MapType::LandWithSeas
              : cfg.mapsList[static_cast<std::size_t>(k) % cfg.mapsList.size()];
    s.seed    = baseSeed + static_cast<uint64_t>(k) * 0x9E3779B97F4A7C15ull;
    return s;
}

} // namespace

void evaluateBalanceIndividual(BalanceIndividual& ind,
                                const BalanceGAConfig& cfg,
                                uint64_t baseSeed,
                                ThreadPool* pool) {
    // Snapshot current global params, install this individual's, restore on exit.
    aoc::balance::BalanceParams& live = aoc::balance::params();
    const aoc::balance::BalanceParams savedParams = live;
    live = ind.genome.toParams();

    // Run the N games (in parallel when a pool is available) under the
    // installed params. No per-player overrides -- balance tuning uses the
    // ship-default leader personalities so we measure the game, not the AI.
    std::vector<SimulationResult> results(static_cast<std::size_t>(cfg.gamesPerEval));

    if (pool != nullptr && cfg.gamesPerEval > 1) {
        std::vector<std::future<SimulationResult>> fs;
        fs.reserve(static_cast<std::size_t>(cfg.gamesPerEval));
        for (int32_t k = 0; k < cfg.gamesPerEval; ++k) {
            const GameSpec s = pickGameSpec(k, cfg, baseSeed);
            const std::atomic<bool>* stop = cfg.stopFlag;
            fs.push_back(pool->submit([s, stop]() {
                return runSimulation(s.turns, s.players, s.seed, stop,
                                     {}, s.map);
            }));
        }
        for (std::size_t k = 0; k < fs.size(); ++k) {
            results[k] = fs[k].get();
        }
    } else {
        for (int32_t k = 0; k < cfg.gamesPerEval; ++k) {
            const GameSpec s = pickGameSpec(k, cfg, baseSeed);
            results[static_cast<std::size_t>(k)] =
                runSimulation(s.turns, s.players, s.seed, cfg.stopFlag, {}, s.map);
        }
    }

    // Aggregate.
    std::array<int32_t, 8> victoryHist{};  // indexed by VictoryType value (0..7)
    int32_t valid = 0;
    int32_t decisive = 0;
    double lenSum = 0.0;
    double lenBudgetSum = 0.0;
    double giniSum = 0.0;
    int32_t giniSamples = 0;

    for (int32_t k = 0; k < cfg.gamesPerEval; ++k) {
        const SimulationResult& r = results[static_cast<std::size_t>(k)];
        if (!r.valid) { continue; }
        ++valid;

        const std::size_t vtIdx = std::min<std::size_t>(7,
            static_cast<std::size_t>(r.victoryType));
        ++victoryHist[vtIdx];
        if (r.victoryType != aoc::sim::VictoryType::None
            && r.victoryType != aoc::sim::VictoryType::Score) {
            ++decisive;
        }

        // Length proxy: we don't have final turn in the result; use the
        // configured budget when victory==None (Score path at turn limit)
        // and a heuristic estimate otherwise. Simulation currently doesn't
        // expose the last turn number, so we approximate:
        //   - None          -> full budget (timed out)
        //   - Score         -> full budget (ran to end)
        //   - any other     -> unknown exact turn; treat as 70% of budget.
        const GameSpec spec = pickGameSpec(k, cfg, baseSeed);
        double lenFrac = 1.0;
        if (r.victoryType == aoc::sim::VictoryType::None
            || r.victoryType == aoc::sim::VictoryType::Score) {
            lenFrac = 1.0;
        } else {
            lenFrac = 0.7;
        }
        lenSum += lenFrac * static_cast<double>(spec.turns);
        lenBudgetSum += static_cast<double>(spec.turns);

        // Gini over eraVP for the surviving civs.
        std::vector<float> vp;
        vp.reserve(r.eraVP.size());
        for (int32_t v : r.eraVP) {
            if (v > 0) { vp.push_back(static_cast<float>(v)); }
        }
        if (!vp.empty()) {
            giniSum += static_cast<double>(gini(vp));
            ++giniSamples;
        }
    }

    BalanceHealth h{};
    if (valid == 0) {
        // Every game aborted (likely stop-flag).  Leave fitness at 0.
        ind.fitness = 0.0f;
        ind.health = h;
        live = savedParams;
        return;
    }

    h.entropy = normalisedEntropy(victoryHist);
    const double avgLenFrac = (lenBudgetSum > 0.0) ? (lenSum / lenBudgetSum) : 1.0;
    h.avgTurns = static_cast<float>(avgLenFrac * (lenSum > 0 ? lenBudgetSum / valid : 0.0));
    h.lengthScore = triangleReward(static_cast<float>(avgLenFrac),
                                    cfg.lengthTargetFrac,
                                    0.15f, 1.0f);

    const float avgGini = (giniSamples > 0)
        ? static_cast<float>(giniSum / static_cast<double>(giniSamples))
        : 0.0f;
    // Healthy band: gini 0.30..0.55 (some inequality, no runaway). Peak 0.4.
    h.giniScore = triangleReward(avgGini, 0.40f, 0.10f, 0.75f);

    h.decisiveShare = static_cast<float>(decisive) / static_cast<float>(valid);

    ind.fitness = cfg.wEntropy  * h.entropy
                + cfg.wLength   * h.lengthScore
                + cfg.wGini     * h.giniScore
                + cfg.wDecisive * h.decisiveShare;
    ind.health  = h;
    ind.gamesPlayed += valid;

    // Restore the previous global params so the caller's state is unchanged.
    live = savedParams;
}

std::vector<BalanceIndividual>
runBalanceGA(const BalanceGAConfig& cfg, uint64_t masterSeed, ThreadPool* pool) {
    std::mt19937 rng(masterSeed);
    const aoc::balance::BalanceBounds bounds = aoc::balance::defaultBalanceBounds();

    std::vector<BalanceIndividual> pop =
        createInitialBalancePopulation(cfg.populationSize, rng, bounds);

    std::fprintf(stderr, "[Balance GA] pop=%d gens=%d games/eval=%d\n",
                 cfg.populationSize, cfg.generations, cfg.gamesPerEval);

    float bestEver = -1.0f;
    BalanceIndividual bestEverInd{};

    for (int32_t gen = 0; gen < cfg.generations; ++gen) {
        std::fprintf(stderr, "\n--- Balance Gen %d/%d ---\n",
                     gen + 1, cfg.generations);

        // Sequential over individuals (singleton clash), parallel over games.
        // All individuals in a generation run the SAME N games so fitness
        // differences reflect genome, not luck of different seeds/maps.
        // Cross-generation variety comes from genSeed changing per gen.
        const uint64_t genSeed = masterSeed + static_cast<uint64_t>(gen) * 1000003ull;
        for (std::size_t i = 0; i < pop.size(); ++i) {
            if (cfg.stopFlag != nullptr
                && cfg.stopFlag->load(std::memory_order_acquire)) {
                std::fprintf(stderr, "[Balance GA] stop requested mid-gen\n");
                goto done;
            }
            evaluateBalanceIndividual(pop[i], cfg, genSeed, pool);
        }

        std::sort(pop.begin(), pop.end(),
                  [](const BalanceIndividual& a, const BalanceIndividual& b) {
                      return a.fitness > b.fitness;
                  });

        const BalanceIndividual& top = pop.front();
        if (top.fitness > bestEver) {
            bestEver = top.fitness;
            bestEverInd = top;
        }

        const aoc::balance::BalanceParams tp = top.genome.toParams();
        std::fprintf(stderr,
            "  best fit=%.4f  ent=%.2f len=%.2f gini=%.2f dec=%.2f\n",
            static_cast<double>(top.fitness),
            static_cast<double>(top.health.entropy),
            static_cast<double>(top.health.lengthScore),
            static_cast<double>(top.health.giniScore),
            static_cast<double>(top.health.decisiveShare));
        std::fprintf(stderr,
            "  top genome: baseLoy=%.2f radius=%d unrest=%d distant=%d "
            "culT=%.0f culW=%d culLead=%.2f integT=%.2f integN=%d "
            "relFrac=%.2f spaceMul=%.2f\n",
            static_cast<double>(tp.baseLoyalty),
            tp.loyaltyPressureRadius, tp.sustainedUnrestTurns,
            tp.distantCityThreshold,
            static_cast<double>(tp.cultureVictoryThreshold),
            tp.cultureVictoryMinWonders,
            static_cast<double>(tp.cultureVictoryLeadRatio),
            static_cast<double>(tp.integrationThreshold),
            tp.integrationTurnsRequired,
            static_cast<double>(tp.religionDominanceFrac),
            static_cast<double>(tp.spaceRaceCostMult));

        if (gen == cfg.generations - 1) { break; }

        // Breed next gen.
        std::vector<BalanceIndividual> next;
        next.reserve(static_cast<std::size_t>(cfg.populationSize));
        for (int32_t e = 0; e < cfg.elitism
             && e < static_cast<int32_t>(pop.size()); ++e) {
            next.push_back(pop[static_cast<std::size_t>(e)]);
        }
        while (static_cast<int32_t>(next.size()) < cfg.populationSize) {
            const BalanceIndividual pa = balanceTournamentSelect(pop, cfg.tournamentSize, rng);
            const BalanceIndividual pb = balanceTournamentSelect(pop, cfg.tournamentSize, rng);
            BalanceIndividual child = balanceCrossover(pa, pb, rng);
            child = balanceMutate(child, bounds, cfg.mutationRate,
                                  cfg.mutationSigma, cfg.resetRate, rng);
            child.fitness = 0.0f;
            child.gamesPlayed = 0;
            next.push_back(child);
        }
        pop = std::move(next);
    }

done:
    std::sort(pop.begin(), pop.end(),
              [](const BalanceIndividual& a, const BalanceIndividual& b) {
                  return a.fitness > b.fitness;
              });
    std::fprintf(stderr, "\n[Balance GA] done. best-ever fit=%.4f\n",
                 static_cast<double>(bestEver));
    return pop;
}

void saveBalanceSummary(const std::vector<BalanceIndividual>& sortedPop,
                         const char* path, int32_t topN) {
    std::ofstream f(path);
    if (!f.is_open()) {
        std::fprintf(stderr, "[Warning] Could not open '%s' for writing\n", path);
        return;
    }
    f << "Balance GA top " << topN << " genomes (game-health fitness)\n";
    f << "========================================================\n\n";

    const int32_t n = std::min(topN, static_cast<int32_t>(sortedPop.size()));
    for (int32_t i = 0; i < n; ++i) {
        const BalanceIndividual& ind = sortedPop[static_cast<std::size_t>(i)];
        const aoc::balance::BalanceParams p = ind.genome.toParams();
        f << "Rank " << (i + 1)
          << "  fitness=" << ind.fitness
          << "  entropy=" << ind.health.entropy
          << "  length=" << ind.health.lengthScore
          << "  gini=" << ind.health.giniScore
          << "  decisive=" << ind.health.decisiveShare
          << "\n";
        f << "  baseLoyalty              = " << p.baseLoyalty              << "\n";
        f << "  loyaltyPressureRadius    = " << p.loyaltyPressureRadius    << "\n";
        f << "  sustainedUnrestTurns     = " << p.sustainedUnrestTurns     << "\n";
        f << "  distantCityThreshold     = " << p.distantCityThreshold     << "\n";
        f << "  cultureVictoryThreshold  = " << p.cultureVictoryThreshold  << "\n";
        f << "  cultureVictoryMinWonders = " << p.cultureVictoryMinWonders << "\n";
        f << "  cultureVictoryLeadRatio  = " << p.cultureVictoryLeadRatio  << "\n";
        f << "  integrationThreshold     = " << p.integrationThreshold     << "\n";
        f << "  integrationTurnsRequired = " << p.integrationTurnsRequired << "\n";
        f << "  religionDominanceFrac    = " << p.religionDominanceFrac    << "\n";
        f << "  spaceRaceCostMult        = " << p.spaceRaceCostMult        << "\n\n";
    }
    f.close();
    std::fprintf(stderr, "[Saved] %s\n", path);
}

} // namespace aoc::ga
