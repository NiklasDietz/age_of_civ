/**
 * @file Notifications.cpp
 * @brief Toast notification system implementation.
 */

#include "aoc/ui/Notifications.hpp"
#include "aoc/ui/BitmapFont.hpp"

#include <renderer/Renderer2D.hpp>

#include <algorithm>

namespace aoc::ui {

void NotificationManager::push(const std::string& message, float duration,
                                float r, float g, float b) {
    Notification notif{};
    notif.message       = message;
    notif.timeRemaining = duration;
    notif.r = r;
    notif.g = g;
    notif.b = b;
    this->m_notifications.push_back(std::move(notif));

    // Trim oldest if exceeding max
    while (this->m_notifications.size() > MAX_VISIBLE * 2) {
        this->m_notifications.erase(this->m_notifications.begin());
    }
}

void NotificationManager::update(float deltaTime) {
    for (Notification& notif : this->m_notifications) {
        notif.timeRemaining -= deltaTime;
    }

    this->m_notifications.erase(
        std::remove_if(this->m_notifications.begin(), this->m_notifications.end(),
            [](const Notification& n) { return n.timeRemaining <= 0.0f; }),
        this->m_notifications.end());
}

void NotificationManager::render(vulkan_app::renderer::Renderer2D& renderer2d,
                                  float screenW, float /*screenH*/,
                                  float pixelScale) const {
    constexpr float MARGIN_RIGHT = 10.0f;
    constexpr float MARGIN_TOP   = 40.0f;
    constexpr float NOTIF_H      = 22.0f;
    constexpr float NOTIF_SPACING = 4.0f;
    constexpr float FONT_SIZE    = 11.0f;
    constexpr float PADDING_X    = 8.0f;

    // Show only the most recent MAX_VISIBLE notifications
    const std::size_t total = this->m_notifications.size();
    const std::size_t startIdx = total > MAX_VISIBLE ? total - MAX_VISIBLE : 0;

    float yOffset = MARGIN_TOP;
    for (std::size_t i = startIdx; i < total; ++i) {
        const Notification& notif = this->m_notifications[i];

        // Fade-out when < 0.5s remaining
        const float alpha = std::min(1.0f, notif.timeRemaining / 0.5f);

        // Measure text width
        const Rect textBounds = BitmapFont::measureText(notif.message, FONT_SIZE);
        const float notifW = textBounds.w + PADDING_X * 2.0f;

        // Position at top-right
        const float nx = (screenW - notifW - MARGIN_RIGHT) * pixelScale;
        const float ny = yOffset * pixelScale;
        const float nw = notifW * pixelScale;
        const float nh = NOTIF_H * pixelScale;

        // Background
        renderer2d.drawFilledRect(nx, ny, nw, nh,
                                   0.08f, 0.08f, 0.12f, 0.85f * alpha);

        // Text
        BitmapFont::drawText(renderer2d, notif.message,
                              nx + PADDING_X * pixelScale,
                              ny + 4.0f * pixelScale,
                              FONT_SIZE,
                              Color{notif.r, notif.g, notif.b, alpha},
                              pixelScale);

        yOffset += NOTIF_H + NOTIF_SPACING;
    }
}

} // namespace aoc::ui
