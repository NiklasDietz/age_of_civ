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
#include <string>
#include <vector>

namespace vulkan_app::renderer {
class Renderer2D;
}

namespace aoc::ui {

struct Notification {
    std::string message;
    float timeRemaining = 3.0f;  ///< Seconds to display
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

class NotificationManager {
public:
    /// Push a new notification message.
    void push(const std::string& message, float duration = 3.0f,
              float r = 1.0f, float g = 1.0f, float b = 1.0f);

    /// Advance timers and remove expired notifications.
    void update(float deltaTime);

    /// Render notifications at the top-right of the screen.
    void render(vulkan_app::renderer::Renderer2D& renderer2d,
                float screenW, float screenH, float pixelScale) const;

private:
    std::vector<Notification> m_notifications;
    static constexpr std::size_t MAX_VISIBLE = 5;
};

} // namespace aoc::ui
