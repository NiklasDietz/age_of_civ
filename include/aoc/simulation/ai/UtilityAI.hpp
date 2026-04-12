#pragma once

/**
 * @file UtilityAI.hpp
 * @brief Response curves and scored considerations for utility-based AI decisions.
 *
 * Provides the mathematical primitives for a Utility AI: response curves that
 * transform a normalized [0,1] input into a [0,1] score, and considerations
 * that bundle a raw input range with a curve. Action scores are the product of
 * all considerations, so any consideration scoring 0 fully disqualifies the
 * action.
 *
 * Usage pattern:
 *   1. Define UtilityConsideration structs with their input ranges and curves.
 *   2. Call consideration.score(rawValue) for each consideration.
 *   3. Multiply all scores together. The result is the action's utility.
 */

#include <cstdint>
#include <cmath>
#include <algorithm>
#include <vector>
#include <functional>

namespace aoc::sim::ai {

// ============================================================================
// Response curve types
// ============================================================================

/**
 * @brief Mathematical curve shapes for mapping normalized [0,1] inputs to
 * [0,1] utility scores.
 *
 * Each shape captures a different relationship between game state and desire:
 *   - Linear:    proportional desire (more gold -> more want to build a market)
 *   - Quadratic: escalating urgency as the situation worsens (HP drops fast near death)
 *   - SquareRoot: diminishing returns (4th city less valuable than 2nd)
 *   - Logistic:  threshold trigger (attack when strength crosses a tipping point)
 *   - Step:      binary on/off (have the prerequisite or you don't)
 *   - Inverse:   inverted desire (more cities means less settler want)
 *   - Constant:  fixed multiplier regardless of input (base weight)
 */
enum class CurveType : uint8_t {
    Linear,
    Quadratic,
    SquareRoot,
    Logistic,
    Step,
    Inverse,
    Constant,
};

// ============================================================================
// UtilityCurve
// ============================================================================

/**
 * @brief Parameterized response curve. Evaluates a normalized [0,1] input
 * and returns a [0,1] score.
 *
 * All factory methods produce curves clamped to [0,1] output.
 *
 * Fields are interpreted per CurveType:
 *   Linear:    y = clamp(slope * x + yShift, 0, 1)
 *   Quadratic: y = clamp(x^exponent, 0, 1)
 *   SquareRoot: y = clamp(sqrt(x), 0, 1)
 *   Logistic:  y = 1 / (1 + exp(-slope * (x - xShift)))
 *   Step:      y = (x >= xShift) ? 1.0 : 0.0
 *   Inverse:   y = clamp(1.0 - x, 0, 1)
 *   Constant:  y = clamp(yShift, 0, 1)
 */
struct UtilityCurve {
    CurveType type     = CurveType::Linear;
    float     slope    = 1.0f;    ///< Linear slope or logistic steepness (k)
    float     exponent = 2.0f;    ///< Power curve exponent (Quadratic uses this)
    float     xShift   = 0.5f;    ///< Logistic midpoint or Step threshold
    float     yShift   = 0.0f;    ///< Vertical offset for Linear; constant value for Constant

    /**
     * @brief Evaluate the curve for a pre-normalized input x in [0,1].
     * @param x  Normalized input. Values outside [0,1] are legal but may yield
     *           out-of-range outputs for non-clamping curves (Logistic).
     * @return Score in [0,1] for all types except unclamped Logistic tails.
     */
    [[nodiscard]] float evaluate(float x) const;

    // -------------------------------------------------------------------------
    // Factory methods for common, well-understood curve shapes
    // -------------------------------------------------------------------------

    /// Linear ramp: y = slope * x + offset. Clamped to [0,1].
    [[nodiscard]] static UtilityCurve linear(float slope = 1.0f, float offset = 0.0f);

    /// Quadratic escalation: y = x^2. Urgency grows fast near x=1.
    [[nodiscard]] static UtilityCurve quadratic();

    /// Square-root diminishing returns: y = sqrt(x).
    [[nodiscard]] static UtilityCurve squareRoot();

    /**
     * @brief Logistic (sigmoid) threshold trigger.
     * @param steepness  How sharply the curve transitions at the midpoint.
     *                   Typical values: 6-12. Higher = more step-like.
     * @param midpoint   x-value where y = 0.5. Normalized [0,1].
     */
    [[nodiscard]] static UtilityCurve logistic(float steepness = 10.0f,
                                               float midpoint  = 0.5f);

    /// Binary step: y = (x >= threshold) ? 1.0 : 0.0.
    [[nodiscard]] static UtilityCurve step(float threshold = 0.5f);

    /// Inverse: y = 1.0 - x. Desire decreases as input increases.
    [[nodiscard]] static UtilityCurve inverse();

    /// Constant multiplier regardless of input. Useful as a base-weight factor.
    [[nodiscard]] static UtilityCurve constant(float value);
};

// ============================================================================
// UtilityConsideration
// ============================================================================

/**
 * @brief A single scored factor in a utility calculation.
 *
 * Binds a raw game-value range to a response curve. Raw inputs outside
 * [inputMin, inputMax] are clamped before normalization so the curve always
 * receives a clean [0,1] value.
 */
struct UtilityConsideration {
    float        inputMin = 0.0f;   ///< Raw value that maps to normalized 0
    float        inputMax = 1.0f;   ///< Raw value that maps to normalized 1
    UtilityCurve curve;

    /**
     * @brief Normalize rawInput to [0,1] then evaluate through the curve.
     * @param rawInput  The un-normalized game value (e.g., city count, treasury).
     * @return Score in [0,1] (Logistic may slightly exceed this at extremes).
     */
    [[nodiscard]] float score(float rawInput) const;
};

// ============================================================================
// Free utilities
// ============================================================================

/**
 * @brief Normalize a raw value into [0,1] given an expected min/max range.
 *
 * When maxVal <= minVal (degenerate range) 0.5 is returned as a neutral score
 * rather than producing a divide-by-zero or an extreme value.
 */
[[nodiscard]] inline float normalizeValue(float value, float minVal, float maxVal) {
    if (maxVal <= minVal) { return 0.5f; }
    return std::clamp((value - minVal) / (maxVal - minVal), 0.0f, 1.0f);
}

} // namespace aoc::sim::ai
