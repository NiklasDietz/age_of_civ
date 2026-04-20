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
    // Loyalty / secession
    float   baseLoyalty              = 5.80f;  ///< Per-turn baseline loyalty
    int32_t loyaltyPressureRadius    = 14;     ///< Hexes a city projects pressure
    int32_t sustainedUnrestTurns     = 8;      ///< Turns below Unrest → secession eligible
    int32_t distantCityThreshold     = 9;      ///< Hexes from capital for periphery secession

    // Victory: culture
    float   cultureVictoryThreshold  = 4246.0f;
    int32_t cultureVictoryMinWonders = 3;
    float   cultureVictoryLeadRatio  = 1.48f;

    // Victory: integration (per-category ratio-to-avg, 6-of-8 cats, N turns)
    float   integrationThreshold     = 1.27f;
    int32_t integrationTurnsRequired = 12;

    // Victory: religion dominance fraction (0..1).
    float   religionDominanceFrac    = 0.77f;

    // Victory: space race cost multiplier (1.0 = nominal SPACE_PROJECT_DEFS).
    float   spaceRaceCostMult        = 1.01f;
};

/// Access the single global balance-params instance.
[[nodiscard]] BalanceParams& params();

/// Gene layout for the balance GA. Order fixed — used by toArray/fromArray.
///
/// Intentionally small (11 knobs). Larger balance genomes become
/// unproductive without much larger populations.
constexpr int32_t BALANCE_PARAM_COUNT = 11;

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
