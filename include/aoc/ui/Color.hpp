#pragma once

/**
 * @file Color.hpp
 * @brief RGBA colour primitive, split out of Widget.hpp.
 *
 * This exists so StyleTokens.hpp can define the palette without pulling in
 * the whole widget tree, which in turn lets Widget.hpp include StyleTokens
 * and default its *Data structs to `tokens::` values. Including Color from
 * both breaks what would otherwise be a circular include.
 *
 * Colours are authored in sRGB as normalized floats; see StyleTokens.hpp.
 */

namespace aoc::ui {

struct Color {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

} // namespace aoc::ui
