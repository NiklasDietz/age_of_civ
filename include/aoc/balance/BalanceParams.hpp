#pragma once

/// @file BalanceParams.hpp
/// @brief Runtime-mutable game balance constants (tunable by balance GA).
///
/// Replaces scattered `constexpr` values at balance-relevant sites with a
/// single, global, mutable container. The defaults match the current
/// compile-time values so leaving the singleton untouched is behavior-
/// equivalent to the pre-refactor build.
///
/// Designed for the "--tune-mode balance" GA in aoc_evolve: the GA writes
/// new values per candidate genome, runs the sim, reads health metrics,
/// and iterates.
///
/// Values are read on every evaluation site. Do NOT cache into local
/// `constexpr` again -- that defeats the purpose.

#include <array>
#include <cstdint>

namespace aoc::balance {

/// All runtime-tunable balance scalars.  Defaults come from the
/// `--tune-mode balance` GA (seed 7547, 8 gens, 12 pop, 5 games/eval over
/// 4 turn lengths * 5 player counts * 4 map types).  Top genome scored
/// fitness 0.87/1.0 (entropy 0.96, length 0.75, gini 0.70, decisive 1.00).
/// Rerun used the post-fix CSI (populated diplomacy avg, non-negative
/// financial) so integration threshold reflects real game dynamics.
struct BalanceParams {
    // WP-O: per-good city stockpile soft cap. Above this, the surplus
    // each turn auto-sells at 0.7× market price to pressure trade flow.
    // Skip food + strategic-late-game uniques (Lithium / Rare Earth /
    // Titanium / He3) so the rule doesn't starve cities or break Mars.
    int32_t stockpileSoftCap         = 80;
    float   stockpileFireSaleMult    = 0.7f;

    // Loyalty / secession.
    // GA-tuned 2026-04-25 (30 gen × 12 pop × 5 games × 800 turns, 4 maps,
    // --balance-winrate, fit=0.9120, ent=0.97): committed top genome.
    float   baseLoyalty              = 2.0f;   ///< GA 2026-04-26
    int32_t loyaltyPressureRadius    = 11;
    int32_t sustainedUnrestTurns     = 8;
    int32_t distantCityThreshold     = 8;

    /// WP-C1: era-indexed foreign-city-pressure decay multiplier. Index is
    /// the player's `currentRevolution` (0..5). Defaults match the legacy
    /// `kForeignDecay` array. Exposed so the BalanceGenome GA can tune.
    /// Own-city pressure decays as 0.80 + 0.20 * this[rev] (in code).
    std::array<float, 6> loyaltyEraDecay =
        {1.00f, 0.95f, 0.85f, 0.75f, 0.65f, 0.55f};

    // Victory: culture. Third retune: even with 7500 threshold, Culture
    // still fired at turn 225 in some sims. Heavy push: 12000 threshold,
    // 7 wonders, 1.5× lead so Culture lands 700-900 consistently.
    // GA-tuned 2026-04-25 with 0.5× culture accumulation rate active.
    float   cultureVictoryThreshold  = 10500.0f; // 2026-04-28 iter10: 10000 → 28%; nudge up
    int32_t cultureVictoryMinWonders = 5;
    float   cultureVictoryLeadRatio  = 1.27f;

    float   integrationThreshold     = 1.66f;  // GA 2026-04-26
    int32_t integrationTurnsRequired = 10;

    // Victory: religion dominance fraction (0..1). Each other civ must have
    // this fraction of its cities following your religion for a religious win.
    // Audit 2026-04: pushed to 0.10 — only 10% of rival cities need to
    // adopt your religion to count that civ as dominated. With multi-
    // religion crowding (every player founds), high fractions are
    // architecturally impossible.
    float   religionDominanceFrac    = 0.08f;  // 2026-04-27: 0.10 still gave 7% RELIGION; eased

    // Victory: space race cost multiplier (1.0 = nominal SPACE_PROJECT_DEFS).
    // Pulled up from GA 0.59 so science path lands similarly-paced to other
    // victory types instead of sprinting; still below 1.0 default so science
    // civs have reachable projects.
    float   spaceRaceCostMult        = 1.70f;  // 2026-04-27 iter8: 1.55 → 31%; bump for ~22%

    // Production-chain tuning (added for the chain-health audit).  GA-tunable
    // scalars that shift recipe output and consumer drain so the balance
    // tuner can search over them instead of relying on hard-coded numbers.
    //
    // chainOutputMult: multiplier applied to output amount for gateway-chain
    //   recipes (OIL→FUEL, OIL→PLASTICS, Electronics, Consumer Goods).
    //   1.0 = no change; higher = more profitable chain → more likely to
    //   be picked by the ranked recipe loop.
    // consumerDemandScale: scales per-population CONSUMER_GOODS /
    //   ADV_CONSUMER_GOODS drain.  1.0 = baseline (pop/3 + 1 per turn).
    float   chainOutputMult          = 1.00f;
    float   consumerDemandScale      = 1.00f;
};

/// Access the single global balance-params instance.
[[nodiscard]] BalanceParams& params();

/// Gene layout for the balance GA. Order fixed — used by toArray/fromArray.
///
/// Slot layout (indices 0-12):
///   0 baseLoyalty, 1 loyaltyPressureRadius, 2 sustainedUnrestTurns,
///   3 distantCityThreshold, 4 cultureVictoryThreshold, 5 cultureVictoryMinWonders,
///   6 cultureVictoryLeadRatio, 7 integrationThreshold, 8 integrationTurnsRequired,
///   9 religionDominanceFrac, 10 spaceRaceCostMult,
///   11 chainOutputMult, 12 consumerDemandScale
constexpr int32_t BALANCE_PARAM_COUNT = 13;

struct BalanceGenome {
    std::array<float, BALANCE_PARAM_COUNT> g{};

    [[nodiscard]] BalanceParams toParams() const;
    void fromParams(const BalanceParams& p);
};

/// Default per-knob bounds for GA search.
struct BalanceBounds {
    std::array<float, BALANCE_PARAM_COUNT> min;
    std::array<float, BALANCE_PARAM_COUNT> max;
};

[[nodiscard]] BalanceBounds defaultBalanceBounds();

} // namespace aoc::balance
