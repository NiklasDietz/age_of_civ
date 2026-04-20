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

/// All runtime-tunable balance scalars. Defaults match current game values.
struct BalanceParams {
    // Loyalty / secession
    float   baseLoyalty              = 4.0f;   ///< Per-turn baseline loyalty
    int32_t loyaltyPressureRadius    = 9;      ///< Hexes a city projects pressure
    int32_t sustainedUnrestTurns     = 3;      ///< Turns below Unrest → secession eligible
    int32_t distantCityThreshold     = 5;      ///< Hexes from capital for periphery secession

    // Victory: culture
    float   cultureVictoryThreshold  = 4000.0f;
    int32_t cultureVictoryMinWonders = 3;
    float   cultureVictoryLeadRatio  = 1.4f;

    // Victory: integration
    float   integrationThreshold     = 1.2f;
    int32_t integrationTurnsRequired = 8;

    // Victory: religion dominance fraction (0..1). Default 0.4 = 40%.
    float   religionDominanceFrac    = 0.4f;

    // Victory: space race cost multiplier (1.0 = current defaults).
    float   spaceRaceCostMult        = 1.0f;
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
