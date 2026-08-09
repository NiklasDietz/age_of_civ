#pragma once

#include "aoc/ui/Widget.hpp"

#include <future>
#include <memory>
#include <string>
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

/// Capture a Vulkan swapchain screenshot to a PNG file and return its path.
/// The HTTP handler places a shared_ptr<promise<string>> here; the drain
/// (render thread) calls captureScreenshot, then fulfils the promise with the
/// path on success or an empty string on failure.
struct TakeScreenshotCommand {
    std::shared_ptr<std::promise<std::string>> result;
};

using UiControlCommand =
    std::variant<ClickWidgetCommand, ClickAtCommand, ScrollAtCommand, TakeScreenshotCommand>;

} // namespace aoc::debug
