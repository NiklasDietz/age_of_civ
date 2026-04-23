#pragma once

/**
 * @file ApplicationHelpers.hpp
 * @brief Small helpers shared between Application.cpp and the HUD
 *        translation unit (Application_HUD.cpp).
 *
 * Kept in src/app/ rather than include/ because these are private to
 * the application layer and should not be exported.
 */

#include "aoc/core/Types.hpp"

#include <cstdint>
#include <string>

namespace aoc::app::detail {

/// Convert a turn number to a year string for HUD display (e.g., "1600 BC").
///
/// Era pacing (years-per-turn):
///   Turn   0-50:  start 4000 BC, 80 years/turn
///   Turn  51-100: start    0 AD, 20 years/turn
///   Turn 101-200: start 1000 AD,  5 years/turn
///   Turn 201-350: start 1500 AD,  3 years/turn
///   Turn 351+:    start 1900 AD,  1 year/turn
inline std::string turnToYear(TurnNumber turn) {
    int32_t year = 0;
    if (turn <= 50) {
        year = -4000 + static_cast<int32_t>(turn) * 80;
    } else if (turn <= 100) {
        year = 0 + static_cast<int32_t>(turn - 51) * 20;
    } else if (turn <= 200) {
        year = 1000 + static_cast<int32_t>(turn - 101) * 5;
    } else if (turn <= 350) {
        year = 1500 + static_cast<int32_t>(turn - 201) * 3;
    } else {
        year = 1900 + static_cast<int32_t>(turn - 351);
    }
    if (year < 0) {
        return std::to_string(-year) + " BC";
    }
    if (year == 0) {
        return "1 AD";
    }
    return std::to_string(year) + " AD";
}

} // namespace aoc::app::detail
