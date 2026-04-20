/**
 * @file main.cpp
 * @brief CLI entry point for the C++ genetic algorithm that optimizes
 *        LeaderBehavior weights via headless simulation tournaments.
 *
 * Mirrors the Python evolve_utility.py interface with equivalent CLI flags.
 * Runs the full evolution loop: population init, fitness evaluation via
 * embedded simulation, tournament selection, crossover, mutation, elitism.
 * Outputs C++ LeaderBehavior initializers and a human-readable summary file.
 */

#include "GeneticAlgorithm.hpp"
#include "FitnessEvaluator.hpp"
#include "ThreadPool.hpp"
#include "BalanceTuner.hpp"

#include "aoc/core/Log.hpp"
#include "aoc/map/MapGenerator.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <numeric>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

/// Set by SIGINT/SIGTERM handler to request graceful shutdown between generations.
std::atomic<bool> g_stopRequested{false};

extern "C" void gaSignalHandler(int /*sig*/) {
    g_stopRequested.store(true, std::memory_order_release);
}

struct CLIArgs {
    int32_t generations  = 50;
    int32_t population   = 20;
    int32_t gamesPerEval = 3;
    int32_t turnsPerGame = 200;
    int32_t playerCount  = 8;
    int32_t workers      = 0;
    bool    quick        = false;
    bool    trackBestGen = false;  ///< Log generation index + elapsed time
                                   ///< for each new best-ever fitness.
    uint64_t seed        = 0;
    bool    seedProvided = false;
    std::vector<int32_t> turnsList;
    std::vector<int32_t> playersList;
    std::vector<aoc::map::MapType> mapsList;
    aoc::ga::OpponentMode opponentMode = aoc::ga::OpponentMode::Fixed;
    int32_t hallOfFameSize = 8;
    /// -1 = all 12 leaders rotate; [0,11] = tune that one leader's archetype.
    int32_t seedLeader = -1;
    /// Balance-winrate: reward rare winning victory types across a generation.
    bool    balanceWinrate = false;
    float   balanceBonus   = 0.8f;
    /// Default: warn — simulation INFO spam (per-turn maintenance/growth/
    /// supply logs) dominates wall time and adds zero value for GA search.
    aoc::log::Severity logLevel = aoc::log::Severity::Warn;
    /// Which GA to run.  "ai" = the leader-behavior tuner (default).
    /// "balance" = the BalanceParams tuner for game-health.
    std::string tuneMode = "ai";
};

/// Parse a single integer argument. Returns false on failure.
[[nodiscard]] bool parseIntArg(const char* value, int32_t& out, const char* name) {
    char* endPtr = nullptr;
    long parsed = std::strtol(value, &endPtr, 10);
    if (endPtr == value || *endPtr != '\0' || parsed <= 0) {
        std::fprintf(stderr, "[Error] Invalid value for %s: '%s' (must be positive integer)\n",
                     name, value);
        return false;
    }
    out = static_cast<int32_t>(parsed);
    return true;
}

/// Parse a comma-separated list of positive integers (e.g. "150,250,400").
[[nodiscard]] bool parseIntList(const char* value, std::vector<int32_t>& out,
                                 const char* name) {
    out.clear();
    const char* p = value;
    while (*p != '\0') {
        char* endPtr = nullptr;
        long parsed = std::strtol(p, &endPtr, 10);
        if (endPtr == p || parsed <= 0) {
            std::fprintf(stderr,
                "[Error] Invalid integer in %s: '%s' (expected comma-separated positive ints)\n",
                name, value);
            return false;
        }
        out.push_back(static_cast<int32_t>(parsed));
        p = endPtr;
        if (*p == ',') {
            ++p;
        } else if (*p != '\0') {
            std::fprintf(stderr,
                "[Error] Invalid separator in %s: '%s' (use commas)\n", name, value);
            return false;
        }
    }
    if (out.empty()) {
        std::fprintf(stderr, "[Error] %s is empty\n", name);
        return false;
    }
    return true;
}

/// Parse a comma-separated list of map-type names (e.g. "continents,archipelago").
/// Empty string is rejected. Unknown names are rejected.
[[nodiscard]] bool parseMapList(const char* value, std::vector<aoc::map::MapType>& out,
                                 const char* name) {
    out.clear();
    std::string buf;
    const char* p = value;
    while (true) {
        if (*p == ',' || *p == '\0') {
            if (buf.empty()) {
                std::fprintf(stderr, "[Error] Empty entry in %s\n", name);
                return false;
            }
            aoc::map::MapType mt{};
            if (!aoc::ga::parseMapType(buf, mt)) {
                std::fprintf(stderr,
                    "[Error] Invalid map type in %s: '%s' "
                    "(expected: continents|pangaea|archipelago|fractal|realistic)\n",
                    name, buf.c_str());
                return false;
            }
            out.push_back(mt);
            buf.clear();
            if (*p == '\0') { break; }
        } else {
            buf.push_back(*p);
        }
        ++p;
    }
    if (out.empty()) {
        std::fprintf(stderr, "[Error] %s is empty\n", name);
        return false;
    }
    return true;
}

/// Parse command-line arguments. Returns false on error.
[[nodiscard]] bool parseCLI(int argc, char* argv[], CLIArgs& args) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--quick") == 0) {
            args.quick = true;
        } else if (std::strcmp(argv[i], "--track-best-gen") == 0) {
            args.trackBestGen = true;
        } else if (std::strcmp(argv[i], "--balance-winrate") == 0) {
            args.balanceWinrate = true;
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::fprintf(stderr,
                "Usage: aoc_evolve [OPTIONS]\n"
                "\n"
                "Options:\n"
                "  --generations N       Number of GA generations (default: 50)\n"
                "  --population N        Population size (default: 20)\n"
                "  --games N             Games per fitness evaluation (default: 3)\n"
                "  --turns N             Turns per simulation game (default: 200)\n"
                "  --players N           Players per game (default: 8)\n"
                "  --turns-list A,B,C    Mixed turn counts, cycled per game\n"
                "                        (e.g. 150,250,400). Overrides --turns.\n"
                "  --players-list A,B,C  Mixed player counts, cycled per game\n"
                "                        (e.g. 4,6,8). Overrides --players.\n"
                "  --maps X,Y,Z          Cycle map types across games.\n"
                "                        Names: continents|pangaea|archipelago|\n"
                "                               fractal|realistic (default: realistic).\n"
                "                        E.g. --maps continents,archipelago,pangaea\n"
                "                        forces each genome to generalize across\n"
                "                        naval, land-war and mixed geography.\n"
                "  --workers N           Thread count (0 = auto-detect, default: 0)\n"
                "  --seed N              RNG seed (default: random)\n"
                "  --opponent-mode MODE  Opponent selection for non-evaluated players:\n"
                "                          fixed    = hand-crafted civ-type personalities\n"
                "                                     (default, deterministic per seed)\n"
                "                          coevolve = random distinct individuals from the\n"
                "                                     current population (true co-evolution)\n"
                "                          champion = samples from the Hall of Fame\n"
                "                                     (top-K best-ever; seeded from hand-\n"
                "                                     crafted leaders so usable at gen 0)\n"
                "                          mixed    = per-slot random mix of the above\n"
                "  --hof-size N          Hall of Fame size for champion/mixed modes\n"
                "                        (default: 8)\n"
                "  --seed-leader N       Tune one specific leader (0..11) by seeding\n"
                "                        the entire population from that archetype.\n"
                "                        Output is that leader's GA-evolved values.\n"
                "                          0=Trajan 1=Cleopatra 2=QinShiHuang 3=Frederick\n"
                "                          4=Pericles 5=Victoria  6=Hojo        7=Cyrus\n"
                "                          8=Montezuma 9=Gandhi  10=Peter     11=PedroII\n"
                "  --log-level LEVEL     Simulation log verbosity:\n"
                "                          debug|info|warn|error|quiet\n"
                "                        Default: warn. The GA does not need the\n"
                "                        per-turn INFO spam from maintenance/supply\n"
                "                        /climate systems -- those fprintfs cost\n"
                "                        real wall time. Use 'info' to debug AI\n"
                "                        behavior, 'quiet' (== fatal) for max speed.\n"
                "  --balance-winrate     Reward genomes that win via currently-rare\n"
                "                        victory types. Builds a per-generation\n"
                "                        histogram of subject wins across games and\n"
                "                        adds a per-game bonus proportional to how\n"
                "                        under-represented that win type is. Pushes\n"
                "                        the GA away from domination-spam convergence.\n"
                "  --balance-bonus F     Max bonus added to the outcome score of a\n"
                "                        win via the rarest type (default: 0.8).\n"
                "                        Ignored when --balance-winrate is off.\n"
                "  --quick               Quick test: 5 gens, 10 pop, 2 games, 150 turns\n"
                "  --track-best-gen      Log the generation at which each new\n"
                "                        best-ever fitness was reached, and print\n"
                "                        a final summary of convergence. Use this\n"
                "                        on long runs to learn how many generations\n"
                "                        you actually need.\n"
                "  --tune-mode MODE      What to optimise:\n"
                "                          ai       = LeaderBehavior genes (default)\n"
                "                          balance  = BalanceParams for game-health\n"
                "                                     (victory-type entropy + length\n"
                "                                     target + score gini). Uses the\n"
                "                                     shipping leaders; tunes the game.\n"
                "  --help, -h            Show this help message\n");
            return false;
        } else if (i + 1 < argc) {
            if (std::strcmp(argv[i], "--generations") == 0) {
                if (!parseIntArg(argv[++i], args.generations, "--generations")) { return false; }
            } else if (std::strcmp(argv[i], "--population") == 0) {
                if (!parseIntArg(argv[++i], args.population, "--population")) { return false; }
            } else if (std::strcmp(argv[i], "--games") == 0) {
                if (!parseIntArg(argv[++i], args.gamesPerEval, "--games")) { return false; }
            } else if (std::strcmp(argv[i], "--turns") == 0) {
                if (!parseIntArg(argv[++i], args.turnsPerGame, "--turns")) { return false; }
            } else if (std::strcmp(argv[i], "--players") == 0) {
                if (!parseIntArg(argv[++i], args.playerCount, "--players")) { return false; }
            } else if (std::strcmp(argv[i], "--turns-list") == 0) {
                if (!parseIntList(argv[++i], args.turnsList, "--turns-list")) { return false; }
            } else if (std::strcmp(argv[i], "--players-list") == 0) {
                if (!parseIntList(argv[++i], args.playersList, "--players-list")) { return false; }
            } else if (std::strcmp(argv[i], "--maps") == 0) {
                if (!parseMapList(argv[++i], args.mapsList, "--maps")) { return false; }
            } else if (std::strcmp(argv[i], "--workers") == 0) {
                char* endPtr = nullptr;
                long parsed = std::strtol(argv[++i], &endPtr, 10);
                if (endPtr == argv[i] || *endPtr != '\0' || parsed < 0) {
                    std::fprintf(stderr, "[Error] Invalid value for --workers: '%s'\n", argv[i]);
                    return false;
                }
                args.workers = static_cast<int32_t>(parsed);
            } else if (std::strcmp(argv[i], "--seed") == 0) {
                char* endPtr = nullptr;
                unsigned long long parsed = std::strtoull(argv[++i], &endPtr, 10);
                if (endPtr == argv[i] || *endPtr != '\0') {
                    std::fprintf(stderr, "[Error] Invalid value for --seed: '%s'\n", argv[i]);
                    return false;
                }
                args.seed = static_cast<uint64_t>(parsed);
                args.seedProvided = true;
            } else if (std::strcmp(argv[i], "--opponent-mode") == 0) {
                const char* val = argv[++i];
                if (!aoc::ga::parseOpponentMode(val, args.opponentMode)) {
                    std::fprintf(stderr,
                        "[Error] Invalid --opponent-mode '%s' "
                        "(expected: fixed|coevolve|champion|mixed)\n", val);
                    return false;
                }
            } else if (std::strcmp(argv[i], "--hof-size") == 0) {
                if (!parseIntArg(argv[++i], args.hallOfFameSize, "--hof-size")) { return false; }
            } else if (std::strcmp(argv[i], "--seed-leader") == 0) {
                char* endPtr = nullptr;
                long parsed = std::strtol(argv[++i], &endPtr, 10);
                if (endPtr == argv[i] || *endPtr != '\0' || parsed < 0 || parsed > 11) {
                    std::fprintf(stderr,
                        "[Error] Invalid --seed-leader '%s' (expected 0..11)\n", argv[i]);
                    return false;
                }
                args.seedLeader = static_cast<int32_t>(parsed);
            } else if (std::strcmp(argv[i], "--balance-bonus") == 0) {
                char* endPtr = nullptr;
                double parsed = std::strtod(argv[++i], &endPtr);
                if (endPtr == argv[i] || *endPtr != '\0' || parsed < 0.0) {
                    std::fprintf(stderr,
                        "[Error] Invalid value for --balance-bonus: '%s'\n", argv[i]);
                    return false;
                }
                args.balanceBonus = static_cast<float>(parsed);
            } else if (std::strcmp(argv[i], "--tune-mode") == 0) {
                const char* val = argv[++i];
                if (std::strcmp(val, "ai") != 0 && std::strcmp(val, "balance") != 0) {
                    std::fprintf(stderr,
                        "[Error] Invalid --tune-mode '%s' (expected: ai|balance)\n", val);
                    return false;
                }
                args.tuneMode = val;
            } else if (std::strcmp(argv[i], "--log-level") == 0) {
                const char* val = argv[++i];
                if      (std::strcmp(val, "debug") == 0) { args.logLevel = aoc::log::Severity::Debug; }
                else if (std::strcmp(val, "info")  == 0) { args.logLevel = aoc::log::Severity::Info;  }
                else if (std::strcmp(val, "warn")  == 0) { args.logLevel = aoc::log::Severity::Warn;  }
                else if (std::strcmp(val, "error") == 0) { args.logLevel = aoc::log::Severity::Error; }
                else if (std::strcmp(val, "quiet") == 0
                      || std::strcmp(val, "fatal") == 0) { args.logLevel = aoc::log::Severity::Fatal; }
                else {
                    std::fprintf(stderr,
                        "[Error] Invalid --log-level '%s' "
                        "(expected: debug|info|warn|error|quiet)\n", val);
                    return false;
                }
            } else {
                std::fprintf(stderr, "[Error] Unknown argument: '%s'\n", argv[i]);
                return false;
            }
        } else {
            std::fprintf(stderr, "[Error] Unknown or incomplete argument: '%s'\n", argv[i]);
            return false;
        }
    }
    return true;
}

/// Save a human-readable summary of the evolved tiers to a text file.
void saveSummary(const aoc::ga::DifficultyTiers& tiers, const char* path) {
    std::ofstream file(path);
    if (!file.is_open()) {
        std::fprintf(stderr, "[Warning] Could not open '%s' for writing\n", path);
        return;
    }

    static constexpr const char* PARAM_NAMES[aoc::ga::NUM_PARAMS] = {
        "militaryAggression", "expansionism", "scienceFocus", "cultureFocus",
        "economicFocus", "diplomaticOpenness", "religiousZeal", "nukeWillingness",
        "trustworthiness", "grudgeHolding",
        "techMilitary", "techEconomic", "techIndustrial", "techNaval", "techInformation",
        "prodSettlers", "prodMilitary", "prodBuilders", "prodBuildings", "prodWonders",
        "prodNaval", "prodReligious",
        "warDeclarationThreshold", "peaceAcceptanceThreshold", "allianceDesire",
        "riskTolerance", "environmentalism", "peripheryTolerance", "greatPersonFocus",
        "espionagePriority", "ideologicalFervor", "speculationAppetite",
        "milBaseWeight", "milThreatSensitivity", "milEmergencySlope", "milOverstockPenalty",
    };

    file << "Evolved Utility AI Weights (C++ GA)\n";
    file << "==================================================\n\n";

    struct TierEntry {
        const char* name;
        const aoc::ga::Individual* individual;
    };
    std::array<TierEntry, 3> entries = {{
        {"Hard",   &tiers.hard},
        {"Medium", &tiers.medium},
        {"Easy",   &tiers.easy},
    }};

    for (const TierEntry& entry : entries) {
        file << entry.name << " AI (fitness=" << entry.individual->fitness << "):\n";
        for (int32_t i = 0; i < aoc::ga::NUM_PARAMS; ++i) {
            std::size_t idx = static_cast<std::size_t>(i);
            // Pad name to 30 chars for alignment
            std::string nameStr(PARAM_NAMES[i]);
            while (nameStr.size() < 30) { nameStr += ' '; }
            file << "  " << nameStr << " = "
                 << entry.individual->genes[idx] << "\n";
        }
        file << "\n";
    }

    file.close();
    std::fprintf(stderr, "[Saved] %s\n", path);
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    CLIArgs args{};
    if (!parseCLI(argc, argv, args)) {
        return 1;
    }

    // Apply --quick overrides
    if (args.quick) {
        args.population   = 10;
        args.generations  = 5;
        args.gamesPerEval = 2;
        args.turnsPerGame = 150;
    }

    // Apply log-level gate: skips formatting + fprintf on filtered messages.
    // Huge wall-time win for info-level sim spam (per-turn maintenance etc).
    aoc::log::setMinSeverity(args.logLevel);

    // Register signal handlers for graceful shutdown (Ctrl-C / kill).
    std::signal(SIGINT,  gaSignalHandler);
    std::signal(SIGTERM, gaSignalHandler);

    // Seed RNG
    uint64_t masterSeed = args.seed;
    if (!args.seedProvided) {
        std::random_device rd;
        masterSeed = (static_cast<uint64_t>(rd()) << 32) | static_cast<uint64_t>(rd());
    }
    std::mt19937 rng(masterSeed);

    // Auto-bump gamesPerEval so every list entry is sampled at least once.
    {
        std::size_t listMax = std::max({args.turnsList.size(),
                                          args.playersList.size(),
                                          args.mapsList.size()});
        if (listMax > 0 && static_cast<std::size_t>(args.gamesPerEval) < listMax) {
            args.gamesPerEval = static_cast<int32_t>(listMax);
            std::fprintf(stderr,
                "[Config] Bumped --games to %d to cover all list entries.\n",
                args.gamesPerEval);
        }
    }

    // Print banner
    std::fprintf(stderr, "============================================================\n");
    std::fprintf(stderr, "Age of Civilization -- C++ Genetic Algorithm for Utility AI\n");
    std::fprintf(stderr, "============================================================\n\n");
    std::fprintf(stderr, "[Config] Population: %d, Generations: %d, Games/eval: %d\n",
                 args.population, args.generations, args.gamesPerEval);
    std::fprintf(stderr, "  Genome: %d parameters (LeaderBehavior)\n", aoc::ga::NUM_PARAMS);
    std::fprintf(stderr, "  Elitism: top 2 preserved\n");
    if (args.turnsList.empty()) {
        std::fprintf(stderr, "  Turns per game: %d\n", args.turnsPerGame);
    } else {
        std::fprintf(stderr, "  Turns per game (cycled):");
        for (int32_t t : args.turnsList) { std::fprintf(stderr, " %d", t); }
        std::fprintf(stderr, "\n");
    }
    if (args.playersList.empty()) {
        std::fprintf(stderr, "  Players per game: %d\n", args.playerCount);
    } else {
        std::fprintf(stderr, "  Players per game (cycled):");
        for (int32_t p : args.playersList) { std::fprintf(stderr, " %d", p); }
        std::fprintf(stderr, "\n");
    }
    if (args.mapsList.empty()) {
        std::fprintf(stderr, "  Map type: realistic\n");
    } else {
        std::fprintf(stderr, "  Map types (cycled):");
        for (aoc::map::MapType m : args.mapsList) {
            std::fprintf(stderr, " %s", aoc::ga::mapTypeName(m));
        }
        std::fprintf(stderr, "\n");
    }
    std::fprintf(stderr, "  Seed: %llu\n", static_cast<unsigned long long>(masterSeed));
    std::fprintf(stderr, "  Opponent mode: %s",
                 aoc::ga::opponentModeName(args.opponentMode));
    if (args.opponentMode == aoc::ga::OpponentMode::Champion
        || args.opponentMode == aoc::ga::OpponentMode::Mixed) {
        std::fprintf(stderr, " (HoF size %d)", args.hallOfFameSize);
    }
    std::fprintf(stderr, "\n");
    if (args.balanceWinrate) {
        std::fprintf(stderr,
            "  Balance-winrate: ON (max bonus %.2f on rarest victory type)\n",
            static_cast<double>(args.balanceBonus));
    }

    // Build GA config
    aoc::ga::GAConfig config{};
    config.populationSize = args.population;
    config.generations    = args.generations;
    config.gamesPerEval   = args.gamesPerEval;
    config.turnsPerGame   = args.turnsPerGame;
    config.playerCount    = args.playerCount;
    config.turnsList      = args.turnsList;
    config.playersList    = args.playersList;
    config.mapsList       = args.mapsList;
    config.elitism        = 2;
    config.tournamentSize = 3;
    config.mutationRate   = 0.2f;
    config.mutationSigma  = 0.15f;
    config.resetRate      = 0.1f;
    config.threadCount    = args.workers;
    config.stopFlag       = &g_stopRequested;
    config.opponentMode   = args.opponentMode;
    config.hallOfFameSize = args.hallOfFameSize;
    config.balanceWinrate = args.balanceWinrate;
    config.balanceBonus   = args.balanceBonus;

    aoc::ga::ParamBounds bounds = aoc::ga::defaultBounds();

    // Persistent pool for all parallel training work (reused across generations
    // and any future parallel tasks: init, sensitivity sweeps, etc).
    std::size_t poolSize = (args.workers > 0)
        ? static_cast<std::size_t>(args.workers)
        : std::thread::hardware_concurrency();
    if (poolSize == 0) { poolSize = 4; }
    aoc::ga::ThreadPool pool(poolSize);
    std::fprintf(stderr, "[Init] ThreadPool: %zu workers\n", pool.size());

    // --tune-mode balance: run the BalanceParams GA and exit.  This tuner
    // has its own loop because its fitness is a population-wide metric
    // (victory-type entropy etc.) rather than single-genome fitness.
    if (args.tuneMode == "balance") {
        aoc::ga::BalanceGAConfig bcfg{};
        bcfg.populationSize = args.population;
        bcfg.generations    = args.generations;
        bcfg.gamesPerEval   = args.gamesPerEval;
        bcfg.turnsPerGame   = args.turnsPerGame;
        bcfg.playerCount    = args.playerCount;
        bcfg.turnsList      = args.turnsList;
        bcfg.playersList    = args.playersList;
        bcfg.mapsList       = args.mapsList;
        bcfg.threadCount    = args.workers;
        bcfg.stopFlag       = &g_stopRequested;

        std::fprintf(stderr,
            "[Mode] BALANCE tuning (game-health).  Subject: game; AI: shipping defaults.\n");

        std::vector<aoc::ga::BalanceIndividual> finalPop =
            aoc::ga::runBalanceGA(bcfg, masterSeed, &pool);
        aoc::ga::saveBalanceSummary(finalPop, "evolved_balance.txt", 5);

        if (!finalPop.empty()) {
            const aoc::balance::BalanceParams tp = finalPop.front().genome.toParams();
            std::fprintf(stderr,
                "\n[Best] fit=%.4f  paste into BalanceParams defaults:\n",
                static_cast<double>(finalPop.front().fitness));
            std::fprintf(stderr,
                "  baseLoyalty=%.2f radius=%d unrest=%d distant=%d cultT=%.0f\n"
                "  cultW=%d cultLead=%.2f integT=%.2f integN=%d relFrac=%.2f space=%.2f\n",
                static_cast<double>(tp.baseLoyalty), tp.loyaltyPressureRadius,
                tp.sustainedUnrestTurns, tp.distantCityThreshold,
                static_cast<double>(tp.cultureVictoryThreshold),
                tp.cultureVictoryMinWonders,
                static_cast<double>(tp.cultureVictoryLeadRatio),
                static_cast<double>(tp.integrationThreshold),
                tp.integrationTurnsRequired,
                static_cast<double>(tp.religionDominanceFrac),
                static_cast<double>(tp.spaceRaceCostMult));
        }
        return 0;
    }

    // Initialize population
    std::vector<aoc::ga::Individual> population =
        aoc::ga::createInitialPopulation(args.population, rng, bounds, args.seedLeader);

    if (args.seedLeader >= 0) {
        static const char* LEADER_NAMES[12] = {
            "Trajan", "Cleopatra", "QinShiHuang", "Frederick",
            "Pericles", "Victoria", "Hojo", "Cyrus",
            "Montezuma", "Gandhi", "Peter", "PedroII"
        };
        std::fprintf(stderr,
            "\n[Init] Population seeded ENTIRELY from leader %d (%s) + mutations\n",
            args.seedLeader, LEADER_NAMES[args.seedLeader]);
    } else {
        int32_t seeded = std::min(12, args.population);
        std::fprintf(stderr,
            "\n[Init] Created population of %d (%d leader seeds + %d mutations)\n",
            args.population, seeded, std::max(0, args.population - seeded));
    }

    // Hall of Fame: top-K individuals ever seen by fitness. Seeded at gen 0
    // from the hand-crafted leader profiles (first 12 entries of the initial
    // population) so Champion/Mixed modes have real opponents from turn 1.
    //
    // fitness is left at the default (0.0f). After gen 0 evaluation, the
    // merge step replaces these with whichever of the current-pop top-K
    // outperforms them -- including the seed leaders themselves, now
    // carrying real fitness.
    std::vector<aoc::ga::Individual> hallOfFame;
    if (args.opponentMode == aoc::ga::OpponentMode::Champion
        || args.opponentMode == aoc::ga::OpponentMode::Mixed) {
        const std::size_t hofCap =
            std::min(static_cast<std::size_t>(args.hallOfFameSize), population.size());
        hallOfFame.reserve(hofCap);
        for (std::size_t i = 0; i < hofCap; ++i) {
            hallOfFame.push_back(population[i]);
            hallOfFame.back().gamesPlayed = 0;
            hallOfFame.back().fitness     = 0.0f;
        }
        std::fprintf(stderr, "[Init] Hall of Fame seeded with %zu hand-crafted leaders\n",
                     hofCap);
    }

    float bestEverFitness = -1e9f;
    aoc::ga::Individual bestEver{};
    int32_t bestEverGen = -1;
    double  bestEverElapsed = 0.0;

    // Convergence history: (generation, bestEver so far). Only populated when
    // --track-best-gen is on. Lets the final summary report when the best
    // genome was actually found so the user can right-size future runs.
    struct BestGenEntry {
        int32_t gen;
        float   fitness;
        double  elapsedSec;
    };
    std::vector<BestGenEntry> bestGenHistory;

    // Rolling window of recent generation durations for ETA.
    std::deque<double> recentGenSeconds;
    constexpr std::size_t ETA_WINDOW = 10;
    std::chrono::steady_clock::time_point trainStart =
        std::chrono::steady_clock::now();

    auto formatHMS = [](double seconds, char* out, std::size_t outSize) {
        if (seconds < 0.0 || !std::isfinite(seconds)) { seconds = 0.0; }
        long total = static_cast<long>(seconds);
        long h = total / 3600;
        long m = (total % 3600) / 60;
        long s = total % 60;
        std::snprintf(out, outSize, "%ldh%02ldm%02lds", h, m, s);
    };

    // Main evolution loop
    for (int32_t gen = 0; gen < args.generations; ++gen) {
        std::chrono::steady_clock::time_point genStart = std::chrono::steady_clock::now();

        std::fprintf(stderr, "\n--- Generation %d/%d ---\n", gen + 1, args.generations);

        // Evaluate fitness for individuals that need it
        uint64_t genSeed = masterSeed + static_cast<uint64_t>(gen) * 1000003;
        const std::vector<aoc::ga::Individual>* hofPtr =
            (args.opponentMode == aoc::ga::OpponentMode::Champion
             || args.opponentMode == aoc::ga::OpponentMode::Mixed)
            ? &hallOfFame : nullptr;
        aoc::ga::evaluatePopulation(population, config, genSeed, &pool, hofPtr);

        // Sort by fitness (descending)
        std::sort(population.begin(), population.end(),
                  [](const aoc::ga::Individual& a, const aoc::ga::Individual& b) {
                      return a.fitness > b.fitness;
                  });

        // Hall of Fame merge: union current-pop top-K with existing HoF,
        // keep top-K by fitness. Preserves historical strong genomes so
        // Champion-mode opponents stay strong even if the population drifts.
        if (args.opponentMode == aoc::ga::OpponentMode::Champion
            || args.opponentMode == aoc::ga::OpponentMode::Mixed) {
            const std::size_t cap = static_cast<std::size_t>(args.hallOfFameSize);
            for (std::size_t i = 0; i < std::min(cap, population.size()); ++i) {
                hallOfFame.push_back(population[i]);
            }
            std::sort(hallOfFame.begin(), hallOfFame.end(),
                      [](const aoc::ga::Individual& a, const aoc::ga::Individual& b) {
                          return a.fitness > b.fitness;
                      });
            if (hallOfFame.size() > cap) { hallOfFame.resize(cap); }
        }

        // Compute generation statistics
        float genBest   = population.front().fitness;
        float genWorst  = population.back().fitness;
        float genMedian = population[population.size() / 2].fitness;
        double genSum = 0.0;
        for (const aoc::ga::Individual& ind : population) {
            genSum += static_cast<double>(ind.fitness);
        }
        double genAvg = genSum / static_cast<double>(population.size());
        double sqSum = 0.0;
        for (const aoc::ga::Individual& ind : population) {
            double d = static_cast<double>(ind.fitness) - genAvg;
            sqSum += d * d;
        }
        double genStddev = std::sqrt(sqSum / static_cast<double>(population.size()));

        std::chrono::steady_clock::time_point genEnd = std::chrono::steady_clock::now();
        double genSeconds = std::chrono::duration<double>(genEnd - genStart).count();

        recentGenSeconds.push_back(genSeconds);
        if (recentGenSeconds.size() > ETA_WINDOW) {
            recentGenSeconds.pop_front();
        }
        double avgRecent = 0.0;
        for (double s : recentGenSeconds) { avgRecent += s; }
        avgRecent /= static_cast<double>(recentGenSeconds.size());
        double totalElapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - trainStart).count();
        int32_t gensRemaining = args.generations - (gen + 1);
        double etaSec = avgRecent * static_cast<double>(gensRemaining);

        if (genBest > bestEverFitness) {
            bestEverFitness = genBest;
            bestEver = population.front();
            bestEverGen = gen + 1;
            bestEverElapsed = totalElapsed;
            if (args.trackBestGen) {
                bestGenHistory.push_back({gen + 1, genBest, totalElapsed});
                std::fprintf(stderr,
                    "  [NewBest] gen %d fitness=%.4f at %.1fs\n",
                    gen + 1, static_cast<double>(genBest), totalElapsed);
            }
        }

        // Cross-gen ASCII progress bar + ETA
        constexpr int32_t BAR_W = 30;
        int32_t gensDone = gen + 1;
        int32_t filled = (gensDone * BAR_W + args.generations / 2) / args.generations;
        char bar[BAR_W + 1];
        for (int32_t i = 0; i < BAR_W; ++i) {
            bar[i] = (i < filled) ? '#' : '.';
        }
        bar[BAR_W] = '\0';
        double pct = 100.0 * static_cast<double>(gensDone)
                   / static_cast<double>(args.generations);
        char elapsedBuf[32];
        char etaBuf[32];
        formatHMS(totalElapsed, elapsedBuf, sizeof(elapsedBuf));
        formatHMS(etaSec,       etaBuf,     sizeof(etaBuf));
        std::fprintf(stderr,
            "  [%s] %d/%d (%.1f%%)  gen %.1fs  avg %.1fs  elapsed %s  ETA %s\n",
            bar, gensDone, args.generations, pct, genSeconds, avgRecent,
            elapsedBuf, etaBuf);

        std::fprintf(stderr,
            "  Fitness: best=%.4f avg=%.4f median=%.4f worst=%.4f stddev=%.4f\n",
            static_cast<double>(genBest), genAvg,
            static_cast<double>(genMedian), static_cast<double>(genWorst),
            genStddev);

        // Top-3 fitnesses
        std::size_t topN = std::min<std::size_t>(3, population.size());
        std::fprintf(stderr, "  Top-%zu:", topN);
        for (std::size_t i = 0; i < topN; ++i) {
            std::fprintf(stderr, " #%zu=%.4f", i + 1,
                         static_cast<double>(population[i].fitness));
        }
        std::fprintf(stderr, "  (best-ever=%.4f)\n",
                     static_cast<double>(bestEverFitness));

        // Show top individual's key traits
        const aoc::ga::Individual& top = population.front();
        std::fprintf(stderr, "  Top: mil=%.2f exp=%.2f sci=%.2f eco=%.2f "
                     "prodSett=%.2f prodMil=%.2f\n",
                     static_cast<double>(top.genes[0]),   // militaryAggression
                     static_cast<double>(top.genes[1]),   // expansionism
                     static_cast<double>(top.genes[2]),   // scienceFocus
                     static_cast<double>(top.genes[4]),   // economicFocus
                     static_cast<double>(top.genes[15]),  // prodSettlers
                     static_cast<double>(top.genes[16])); // prodMilitary

        // Per-generation checkpoint: overwrite so the latest tiers are always on disk.
        // If the run is killed mid-evolution the user still has usable weights.
        {
            aoc::ga::DifficultyTiers checkpointTiers = aoc::ga::extractTiers(population);
            saveSummary(checkpointTiers, "ga_checkpoint.txt");
        }

        // Graceful shutdown: handle SIGINT/SIGTERM between generations.
        if (g_stopRequested.load(std::memory_order_acquire)) {
            std::fprintf(stderr,
                "\n[Signal] Stop requested after generation %d. "
                "Writing final summary from current population.\n",
                gen + 1);
            break;
        }

        // Skip breeding on last generation
        if (gen == args.generations - 1) {
            break;
        }

        // Create next generation
        std::vector<aoc::ga::Individual> nextPopulation;
        nextPopulation.reserve(static_cast<std::size_t>(args.population));

        // Elitism: preserve top N unchanged
        for (int32_t e = 0; e < config.elitism && e < static_cast<int32_t>(population.size()); ++e) {
            nextPopulation.push_back(population[static_cast<std::size_t>(e)]);
        }

        // Breed the rest via tournament selection + crossover + mutation
        while (static_cast<int32_t>(nextPopulation.size()) < args.population) {
            aoc::ga::Individual parentA = aoc::ga::tournamentSelect(
                population, config.tournamentSize, rng);
            aoc::ga::Individual parentB = aoc::ga::tournamentSelect(
                population, config.tournamentSize, rng);
            aoc::ga::Individual child = aoc::ga::crossover(parentA, parentB, rng);
            child = aoc::ga::mutate(child, config.mutationRate, config.mutationSigma,
                                    config.resetRate, bounds, rng);
            nextPopulation.push_back(child);
        }

        population = std::move(nextPopulation);
    }

    // Extract difficulty tiers from final sorted population
    std::fprintf(stderr, "\n============================================================\n");
    std::fprintf(stderr, "EVOLVED DIFFICULTY TIERS\n");
    std::fprintf(stderr, "============================================================\n");

    aoc::ga::DifficultyTiers tiers = aoc::ga::extractTiers(population);

    aoc::ga::printAsCppInitializer(tiers.hard, "Hard");
    aoc::ga::printAsCppInitializer(tiers.medium, "Medium");
    aoc::ga::printAsCppInitializer(tiers.easy, "Easy");

    // Save summary file
    saveSummary(tiers, "evolved_summary.txt");

    std::fprintf(stderr, "\n[Done] Best ever fitness: %.4f\n",
                 static_cast<double>(bestEverFitness));

    if (args.trackBestGen) {
        std::fprintf(stderr, "\n============================================================\n");
        std::fprintf(stderr, "CONVERGENCE SUMMARY\n");
        std::fprintf(stderr, "============================================================\n");
        if (bestEverGen > 0) {
            std::fprintf(stderr,
                "Best-ever fitness %.4f first reached at generation %d of %d (%.1fs).\n",
                static_cast<double>(bestEverFitness), bestEverGen,
                args.generations, bestEverElapsed);
            const int32_t wastedGens = args.generations - bestEverGen;
            if (wastedGens > 0) {
                std::fprintf(stderr,
                    "%d generation(s) after the best did not improve -- consider "
                    "running ~%d generations next time.\n",
                    wastedGens, bestEverGen);
            }
        }
        std::fprintf(stderr, "\nNew-best timeline (%zu entries):\n",
                     bestGenHistory.size());
        for (const BestGenEntry& e : bestGenHistory) {
            std::fprintf(stderr, "  gen %4d  fit=%.4f  t=%.1fs\n",
                         e.gen, static_cast<double>(e.fitness), e.elapsedSec);
        }
    }

    return 0;
}
