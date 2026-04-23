#pragma once

/**
 * @file WidgetInspector.hpp
 * @brief Dev-time overlay that dumps the live widget tree, highlights
 *        the hovered widget, and exposes basic stats.
 *
 * Toggled by a hotkey (default F11). Kept behind a separate header so
 * shipping builds can compile it out without touching UIManager.
 *
 * Usage:
 * ```
 * WidgetInspector inspector;
 * // in frame loop:
 * if (F11 pressed) { inspector.toggle(); }
 * inspector.render(renderer, uiManager, mouseX, mouseY);
 * ```
 */

#include <string>

namespace vulkan_app::renderer { class Renderer2D; }

namespace aoc::ui {

class UIManager;

class WidgetInspector {
public:
    void toggle() { this->m_enabled = !this->m_enabled; }
    void setEnabled(bool on) { this->m_enabled = on; }
    [[nodiscard]] bool isEnabled() const { return this->m_enabled; }

    /// Overlay the widget tree stats + hovered-widget bounds.
    /// No-op when disabled.
    void render(vulkan_app::renderer::Renderer2D& renderer,
                const UIManager& ui,
                float mouseX, float mouseY) const;

    /// Dump the current widget tree to a string (delegates to
    /// `UIManager::dumpTreeJson`). Useful from the dev console.
    [[nodiscard]] static std::string dump(const UIManager& ui);

private:
    bool m_enabled = false;
};

} // namespace aoc::ui
