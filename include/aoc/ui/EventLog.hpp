#pragma once

/**
 * @file EventLog.hpp
 * @brief Turn-based event log for displaying significant game events to the player.
 *
 * Collects events during turn processing (tech completions, unit productions,
 * combat results, etc.) and renders them as an overlay panel.
 */

#include <cstddef>
#include <string>
#include <vector>

namespace vulkan_app::renderer {
class Renderer2D;
}

namespace aoc::ui {

class EventLog {
public:
    /// Add a new event message to the log.
    void addEvent(const std::string& message);

    /// Clear all events (called at the start of each turn).
    void clear();

    /// Render the event log as a semi-transparent panel.
    void render(vulkan_app::renderer::Renderer2D& renderer2d,
                float x, float y, float w, float h, float pixelScale) const;

    /// Get all current events.
    [[nodiscard]] const std::vector<std::string>& events() const;

private:
    std::vector<std::string> m_events;
    static constexpr std::size_t MAX_EVENTS = 50;
};

} // namespace aoc::ui
