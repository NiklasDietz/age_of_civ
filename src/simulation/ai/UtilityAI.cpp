/**
 * @file UtilityAI.cpp
 * @brief Implementations of UtilityCurve::evaluate(), factory helpers, and
 * UtilityConsideration::score().
 */

#include "aoc/simulation/ai/UtilityAI.hpp"

#include <cassert>
#include <cmath>

namespace aoc::sim::ai {

// ============================================================================
// UtilityCurve::evaluate
// ============================================================================

float UtilityCurve::evaluate(float x) const {
    switch (this->type) {
        case CurveType::Linear: {
            return std::clamp(this->slope * x + this->yShift, 0.0f, 1.0f);
        }

        case CurveType::Quadratic: {
            // Generalized power curve: y = x^exponent
            // Clamped so negative inputs don't produce NaN from fractional exponents.
            const float clamped = std::clamp(x, 0.0f, 1.0f);
            return std::clamp(std::pow(clamped, this->exponent), 0.0f, 1.0f);
        }

        case CurveType::SquareRoot: {
            const float clamped = std::clamp(x, 0.0f, 1.0f);
            return std::clamp(std::sqrt(clamped), 0.0f, 1.0f);
        }

        case CurveType::Logistic: {
            // y = 1 / (1 + exp(-slope * (x - xShift)))
            // slope is the steepness (k), xShift is the midpoint.
            // Not clamped: the sigmoid naturally stays in (0,1).
            const float exponentArg = -this->slope * (x - this->xShift);
            return 1.0f / (1.0f + std::exp(exponentArg));
        }

        case CurveType::Step: {
            // Hard binary: 1.0 at or above threshold, 0.0 below.
            return (x >= this->xShift) ? 1.0f : 0.0f;
        }

        case CurveType::Inverse: {
            // y = 1 - x (desire falls as input rises)
            return std::clamp(1.0f - x, 0.0f, 1.0f);
        }

        case CurveType::Constant: {
            // yShift stores the constant value.
            return std::clamp(this->yShift, 0.0f, 1.0f);
        }
    }

    // Unreachable if all enum values are handled, but required for some compilers.
    assert(false && "UtilityAI.cpp: unhandled CurveType in evaluate()");
    return 0.0f;
}

// ============================================================================
// Factory methods
// ============================================================================

UtilityCurve UtilityCurve::linear(float slope, float offset) {
    UtilityCurve c;
    c.type   = CurveType::Linear;
    c.slope  = slope;
    c.yShift = offset;
    return c;
}

UtilityCurve UtilityCurve::quadratic() {
    UtilityCurve c;
    c.type     = CurveType::Quadratic;
    c.exponent = 2.0f;
    return c;
}

UtilityCurve UtilityCurve::squareRoot() {
    UtilityCurve c;
    c.type = CurveType::SquareRoot;
    return c;
}

UtilityCurve UtilityCurve::logistic(float steepness, float midpoint) {
    UtilityCurve c;
    c.type   = CurveType::Logistic;
    c.slope  = steepness;
    c.xShift = midpoint;
    return c;
}

UtilityCurve UtilityCurve::step(float threshold) {
    UtilityCurve c;
    c.type   = CurveType::Step;
    c.xShift = threshold;
    return c;
}

UtilityCurve UtilityCurve::inverse() {
    UtilityCurve c;
    c.type = CurveType::Inverse;
    return c;
}

UtilityCurve UtilityCurve::constant(float value) {
    UtilityCurve c;
    c.type   = CurveType::Constant;
    c.yShift = value;
    return c;
}

// ============================================================================
// UtilityConsideration::score
// ============================================================================

float UtilityConsideration::score(float rawInput) const {
    const float normalized = normalizeValue(rawInput, this->inputMin, this->inputMax);
    return this->curve.evaluate(normalized);
}

} // namespace aoc::sim::ai
