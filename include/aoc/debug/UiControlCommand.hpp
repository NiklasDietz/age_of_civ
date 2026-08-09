#pragma once

#include "aoc/ui/Widget.hpp"

#include <variant>

namespace aoc::debug {

/// Fire a widget's primary click handler directly by id (see
/// `aoc::ui::UIManager::clickWidget`). Bypasses hit-testing.
struct ClickWidgetCommand {
    aoc::ui::WidgetId widgetId;
};

/// Synthesize a real mouse click at a screen coordinate (press then
/// release in the same drain pass), reusing `UIManager::handleInput`'s
/// full dispatch -- the only way to select a specific tab in a
/// `TabBarData` or interact with a `SliderData`, both of which are
/// inherently coordinate-dependent.
struct ClickAtCommand {
    float x;
    float y;
};

/// Synthesize a scroll-wheel event at a screen coordinate.
struct ScrollAtCommand {
    float x;
    float y;
    float delta;
    bool shiftHeld;
};

using UiControlCommand = std::variant<ClickWidgetCommand, ClickAtCommand, ScrollAtCommand>;

} // namespace aoc::debug
