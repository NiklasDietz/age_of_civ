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
    // Loyalty / secession.
    // GA-tuned 2026-04 (25 gen × 10 pop × 5 games × 250 turns, 4 map
    // types, fit=0.8497, ent=0.97): tighter radius + aggressive
    // periphery secession produced the highest victory-type diversity.
    float   baseLoyalty              = 10.00f; ///< Per-turn baseline loyalty
    int32_t loyaltyPressureRadius    = 6;      ///< Hexes a city projects pressure
    int32_t sustainedUnrestTurns     = 8;      ///< Turns below Unrest → secession eligible
    int32_t distantCityThreshold     = 4;      ///< Hexes from capital for periphery secession

    /// WP-C1: era-indexed foreign-city-pressure decay multiplier. Index is
    /// the player's `currentRevolution` (0..5). Defaults match the legacy
    /// `kForeignDecay` array. Exposed so the BalanceGenome GA can tune.
    /// Own-city pressure decays as 0.80 + 0.20 * this[rev] (in code).
    std::array<float, 6> loyaltyEraDecay =
        {1.00f, 0.95f, 0.85f, 0.75f, 0.65f, 0.55f};

    // Victory: culture. Third retune: even with 7500 threshold, Culture
    // still fired at turn 225 in some sims. Heavy push: 12000 threshold,
    // 7 wonders, 1.5× lead so Culture lands 700-900 consistently.
    // With culture accumulation scaled 0.5× in VictoryCondition.cpp,
    // 18000 threshold equals 36000 at raw rate — targets ~turn 800-1000
    // for Culture decision.
    float   cultureVictoryThreshold  = 18000.0f;
    int32_t cultureVictoryMinWonders = 7;
    float   cultureVictoryLeadRatio  = 1.45f;

    // Victory: integration (per-category ratio-to-avg, 6-of-8 cats, N turns).
    // Middle-ground between GA-tuned 1.01 / 6 (too easy) and default 1.27 / 12.
    float   integrationThreshold     = 1.18f;
    int32_t integrationTurnsRequired = 10;

    // Victory: religion dominance fraction (0..1). Each other civ must have
    // this fraction of its cities following your religion for a religious win.
    // Audit 2026-04: 0.45 produced 0 religion wins in 20×1500t; dropped to
    // 0.30 so a missionary-focused civ can plausibly hit the 3-of-4 gate.
    float   religionDominanceFrac    = 0.30f;

    // Victory: space race cost multiplier (1.0 = nominal SPACE_PROJECT_DEFS).
    // Pulled up from GA 0.59 so science path lands similarly-paced to other
    // victory types instead of sprinting; still below 1.0 default so science
    // civs have reachable projects.
    float   spaceRaceCostMult        = 0.60f;  // cheaper so Science wins land 700-900

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
