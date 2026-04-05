#pragma once

/**
 * @file Widget.hpp
 * @brief Core UI types: Rect, Color, WidgetId, and the Widget variant struct.
 *
 * The UI is a retained widget tree. Each widget is a flat struct with a
 * variant payload for type-specific data. No inheritance -- all widgets
 * are stored in a contiguous vector inside UIManager.
 */

#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <variant>
#include <vector>

namespace aoc::ui {

// ============================================================================
// Primitive types
// ============================================================================

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    [[nodiscard]] bool contains(float px, float py) const {
        return px >= this->x && px < this->x + this->w
            && py >= this->y && py < this->y + this->h;
    }
};

struct Color {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

struct Padding {
    float top    = 0.0f;
    float right  = 0.0f;
    float bottom = 0.0f;
    float left   = 0.0f;
};

using WidgetId = uint32_t;
inline constexpr WidgetId INVALID_WIDGET = std::numeric_limits<WidgetId>::max();

// ============================================================================
// Widget type-specific data
// ============================================================================

/// Empty panel -- just a colored rectangle container.
struct PanelData {
    Color backgroundColor = {0.1f, 0.1f, 0.15f, 0.85f};
    float cornerRadius    = 4.0f;
};

/// Clickable button.
struct ButtonData {
    std::string label;
    Color normalColor   = {0.25f, 0.25f, 0.30f, 0.9f};
    Color hoverColor    = {0.35f, 0.35f, 0.40f, 0.9f};
    Color pressedColor  = {0.15f, 0.15f, 0.20f, 0.9f};
    Color labelColor    = {1.0f, 1.0f, 1.0f, 1.0f};
    float cornerRadius  = 3.0f;
    float fontSize      = 14.0f;
    std::function<void()> onClick;
};

/// Text label.
struct LabelData {
    std::string text;
    Color       color    = {1.0f, 1.0f, 1.0f, 1.0f};
    float       fontSize = 14.0f;
};

/// Scrollable list container. Children outside the visible window are skipped during rendering.
struct ScrollListData {
    Color backgroundColor = {0.12f, 0.12f, 0.16f, 0.9f};
    float scrollOffset = 0.0f;     ///< Pixels scrolled from top (0 = no scroll)
    float contentHeight = 0.0f;    ///< Total height of all children (computed during layout)
};

/// Horizontal or vertical layout direction for children.
enum class LayoutDirection : uint8_t {
    Vertical,
    Horizontal,
};

// ============================================================================
// Widget struct (variant-based, stored in flat vector)
// ============================================================================

struct Widget {
    WidgetId id = INVALID_WIDGET;

    /// Requested position/size (input to layout). Absolute or relative depending on parent.
    Rect requestedBounds = {};

    /// Computed absolute bounds (output of layout).
    Rect computedBounds = {};

    Padding padding = {};

    /// Layout direction for child arrangement.
    LayoutDirection layoutDirection = LayoutDirection::Vertical;
    float childSpacing = 4.0f;

    /// If true, size is auto-computed from children.
    bool autoWidth  = false;
    bool autoHeight = false;

    /// Visibility and interaction state.
    bool isVisible = true;
    bool isHovered = false;
    bool isPressed = false;

    /// Tree structure (indices into the UIManager widget vector).
    WidgetId parent = INVALID_WIDGET;
    std::vector<WidgetId> children;

    /// Type-specific payload.
    std::variant<PanelData, ButtonData, LabelData, ScrollListData> data = PanelData{};
};

} // namespace aoc::ui
