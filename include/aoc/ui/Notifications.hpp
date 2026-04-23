#pragma once

/**
 * @file Notifications.hpp
 * @brief Toast notification system for in-game events.
 *
 * Notifications are stacked at the top-right of the screen and fade out
 * after their display duration expires.
 */

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace vulkan_app::renderer {
class Renderer2D;
}

namespace aoc::ui {

/// Priority level for toast ordering. Higher priority pushes earlier.
enum class NotificationPriority : uint8_t {
    Low     = 0,
    Normal  = 1,
    High    = 2,
    Urgent  = 3,
};

struct Notification {
    std::string message;
    float timeRemaining = 3.0f;  ///< Seconds to display
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    NotificationPriority priority = NotificationPriority::Normal;

    /// Optional primary action. When set, the toast renders an inline
    /// button with `actionLabel` and dispatches `onAction()` on click.
    /// Current toast renderer is text-only; this reserves the schema
    /// for the upcoming interactive-toast path without breaking the
    /// existing pipeline.
    std::string actionLabel;
    std::function<void()> onAction;
};

class NotificationManager {
public:
    /// Push a new notification message.
    void push(const std::string& message, float duration = 3.0f,
              float r = 1.0f, float g = 1.0f, float b = 1.0f);

    /// Push an actionable toast. Caller supplies a button label and
    /// click handler that fires when the user accepts.
    void pushAction(std::string message, std::string actionLabel,
                    std::function<void()> onAction, float duration = 6.0f,
                    NotificationPriority priority = NotificationPriority::High);

    /// Advance timers and remove expired notifications.
    void update(float deltaTime);

    /// Render notifications at the top-right of the screen.
    void render(vulkan_app::renderer::Renderer2D& renderer2d,
                float screenW, float screenH, float pixelScale) const;

    /// Read-only view over queued notifications. Used by the planned
    /// UIManager-based interactive toast renderer.
    [[nodiscard]] const std::vector<Notification>& queue() const {
        return this->m_notifications;
    }

    /// Dispatch the action handler of the topmost actionable toast and
    /// drop it from the queue. Used by the toast widget when the user
    /// clicks the inline action button.
    void fireTopAction();

private:
    std::vector<Notification> m_notifications;
    static constexpr std::size_t MAX_VISIBLE = 5;
};

} // namespace aoc::ui
