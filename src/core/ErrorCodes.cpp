/**
 * @file ErrorCodes.cpp
 * @brief Compilation unit for ErrorCodes -- ensures ODR compliance.
 *
 * The constexpr describeError() is defined in the header. This file exists
 * so that the aoc_lib target has at least one core source file and to
 * anchor the translation unit for future non-constexpr error utilities.
 */

#include "aoc/core/ErrorCodes.hpp"

namespace aoc {

// Future: runtime error formatting, logging integration, etc.

} // namespace aoc
