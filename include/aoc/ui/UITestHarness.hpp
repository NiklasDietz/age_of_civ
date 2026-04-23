#pragma once

/**
 * @file UITestHarness.hpp
 * @brief Minimal in-process harness for testing widget-tree layout
 *        without the Vulkan renderer.
 *
 * Exposed as a free-function facade so tests can build a UIManager,
 * create widgets, run layout at a given viewport size, and snapshot
 * computed bounds for golden-file diffs.
 */

#include <string>

namespace aoc::ui {

class UIManager;

/// Snapshot a UIManager's computed widget bounds to a deterministic
/// string (one widget per line: `id kind x y w h`). Suitable for
/// golden-file comparisons in unit tests.
[[nodiscard]] std::string uiSnapshotComputedBounds(const UIManager& ui);

/// Stress-test scaffold: build the given fn callback N times with
/// alternating viewport sizes, call layout each cycle, return true
/// if no asserts fire and the final widget count matches the first
/// cycle (i.e., no widget leak).
bool uiStressResizeCycle(UIManager& ui, int32_t cycles,
                          float widthA, float heightA,
                          float widthB, float heightB);

} // namespace aoc::ui
