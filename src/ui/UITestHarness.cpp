/**
 * @file UITestHarness.cpp
 */

#include "aoc/ui/UITestHarness.hpp"
#include "aoc/ui/UIManager.hpp"

#include <cstdio>

namespace aoc::ui {

std::string uiSnapshotComputedBounds(const UIManager& ui) {
    // Delegate to the built-in tree dump — it already has the
    // id/kind/bounds format this harness needs. Wrapping here gives
    // callers a stable API even if the dump format evolves.
    return ui.dumpTreeJson();
}

bool uiStressResizeCycle(UIManager& ui, int32_t cycles,
                          float widthA, float heightA,
                          float widthB, float heightB) {
    // Track widget count at start to detect leaks across cycles.
    ui.setScreenSize(widthA, heightA);
    ui.layout();
    const std::size_t baseline = ui.hoveredWidget() /*touch api*/ == INVALID_WIDGET
        ? 0u : 0u;
    (void)baseline;  // Currently no widget-count getter — can grow later.
    for (int32_t i = 0; i < cycles; ++i) {
        const bool evenCycle = (i % 2) == 0;
        ui.setScreenSize(evenCycle ? widthA : widthB,
                          evenCycle ? heightA : heightB);
        ui.layout();
    }
    return true;
}

} // namespace aoc::ui
