/**
 * @file Theme.cpp
 */

#include "aoc/ui/Theme.hpp"

namespace aoc::ui {

Theme& theme() {
    // Single-threaded UI layer — a plain static suffices.
    static Theme s_theme;
    return s_theme;
}

} // namespace aoc::ui
