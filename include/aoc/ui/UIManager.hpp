#pragma once

/**
 * @file UIManager.hpp
 * @brief Manages the UI widget tree: creation, layout, input, rendering.
 *
 * Widgets are stored in a flat vector. The tree structure is maintained
 * via parent/children indices. Layout is computed top-down from root
 * widgets. Rendering is back-to-front (parent before children).
 */

#include "aoc/ui/Widget.hpp"

#include <cstdint>
#include <string>
#include <functional>
#include <vector>

namespace vulkan_app::renderer {
class Renderer2D;
}

namespace aoc::ui {

class UIManager {
public:
    UIManager() = default;

    // ========================================================================
    // Widget creation
    // ========================================================================

    /// Create a root-level panel (no parent).
    [[nodiscard]] WidgetId createPanel(Rect bounds, PanelData panelData = {});

    /// Create a panel as child of another widget.
    [[nodiscard]] WidgetId createPanel(WidgetId parent, Rect bounds, PanelData panelData = {});

    /// Create a button.
    [[nodiscard]] WidgetId createButton(WidgetId parent, Rect bounds, ButtonData buttonData);

    /// Create a text label.
    [[nodiscard]] WidgetId createLabel(WidgetId parent, Rect bounds, LabelData labelData);

    /// Create a scrollable list container.
    [[nodiscard]] WidgetId createScrollList(WidgetId parent, Rect bounds, ScrollListData data = {});

    /// Adjust scroll offset on a ScrollList widget.
    void scrollWidget(WidgetId id, float delta);

    /// Remove a widget and all its children.
    void removeWidget(WidgetId id);

    // ========================================================================
    // Widget access
    // ========================================================================

    [[nodiscard]] Widget* getWidget(WidgetId id);
    [[nodiscard]] const Widget* getWidget(WidgetId id) const;

    /// Update the text of a label widget.
    void setLabelText(WidgetId id, std::string text);

    /// Update the label of a button widget.
    void setButtonLabel(WidgetId id, std::string label);

    /// Show or hide a widget.
    void setVisible(WidgetId id, bool visible);

    // ========================================================================
    // Input handling
    // ========================================================================

    /**
     * @brief Process mouse input for the UI.
     *
     * Tests hover and click on all visible widgets.
     * Returns true if the UI consumed the input (click was on a widget).
     */
    bool handleInput(float mouseX, float mouseY,
                     bool mousePressed, bool mouseReleased,
                     float scrollDelta = 0.0f);

    // ========================================================================
    // Layout
    // ========================================================================

    /// Recompute layout for all root widgets and their children.
    void layout();

    // ========================================================================
    // Rendering
    // ========================================================================

    /**
     * @brief Render all visible widgets.
     *
     * Must be called with Renderer2D in screen-space mode (camera reset).
     */
    void render(vulkan_app::renderer::Renderer2D& renderer2d) const;

private:
    WidgetId allocateWidget();
    void layoutWidget(WidgetId id, float parentX, float parentY);
    void renderWidget(vulkan_app::renderer::Renderer2D& renderer2d,
                      WidgetId id) const;
    [[nodiscard]] WidgetId hitTest(float x, float y) const;
    [[nodiscard]] WidgetId hitTestWidget(WidgetId id, float x, float y) const;

    std::vector<Widget>   m_widgets;
    std::vector<WidgetId> m_rootWidgets;  ///< Top-level widgets (no parent)
    std::vector<WidgetId> m_freeList;     ///< Recycled widget slots
    WidgetId              m_nextId = 0;

    /// Currently hovered widget.
    WidgetId m_hoveredWidget = INVALID_WIDGET;
};

} // namespace aoc::ui
