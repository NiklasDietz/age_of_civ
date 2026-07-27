#pragma once

/**
 * @file BalanceMetrics.hpp
 * @brief Pure game-health scoring metrics for the balance GA — Gini
 *        coefficient, triangle reward, and normalised Shannon entropy.
 *
 * Header-only (inline) so unit tests can exercise the math directly without
 * linking the tuner. All functions are pure and deterministic.
 */

#include "aoc/simulation/victory/VictoryCondition.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace aoc::ga {

/// Gini coefficient on a non-negative vector. Returns 0 for empty/all-zero,
/// approaching 1 as inequality increases.
[[nodiscard]] inline float gini(const std::vector<float>& xs) {
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
[[nodiscard]] inline float triangleReward(float value, float target,
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
[[nodiscard]] inline float normalisedEntropy(
    const std::array<int32_t, aoc::sim::VICTORY_TYPE_COUNT>& hist) {
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

} // namespace aoc::ga
