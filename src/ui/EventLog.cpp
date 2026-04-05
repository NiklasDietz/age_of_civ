/**
 * @file EventLog.cpp
 * @brief Turn event log implementation.
 */

#include "aoc/ui/EventLog.hpp"
#include "aoc/ui/BitmapFont.hpp"
#include "aoc/ui/Widget.hpp"

#include <renderer/Renderer2D.hpp>

namespace aoc::ui {

void EventLog::addEvent(const std::string& message) {
    this->m_events.push_back(message);
    if (this->m_events.size() > MAX_EVENTS) {
        this->m_events.erase(this->m_events.begin());
    }
}

void EventLog::clear() {
    this->m_events.clear();
}

void EventLog::render(vulkan_app::renderer::Renderer2D& renderer2d,
                       float x, float y, float w, float h,
                       float pixelScale) const {
    if (this->m_events.empty()) {
        return;
    }

    // Draw semi-transparent background panel
    renderer2d.drawFilledRect(x, y, w, h, 0.05f, 0.05f, 0.08f, 0.70f);

    // Draw events, most recent at bottom
    constexpr float FONT_SIZE  = 11.0f;
    constexpr float LINE_HEIGHT = 14.0f;
    constexpr float PADDING    = 6.0f;

    const float maxLines = (h - PADDING * 2.0f) / LINE_HEIGHT;
    const std::size_t linesToShow = static_cast<std::size_t>(maxLines);
    const std::size_t startIdx = (this->m_events.size() > linesToShow)
        ? this->m_events.size() - linesToShow
        : 0;

    float lineY = y + PADDING;
    for (std::size_t i = startIdx; i < this->m_events.size(); ++i) {
        BitmapFont::drawText(renderer2d, this->m_events[i],
                              x + PADDING, lineY,
                              FONT_SIZE,
                              Color{0.85f, 0.85f, 0.70f, 1.0f},
                              pixelScale);
        lineY += LINE_HEIGHT;
    }
}

const std::vector<std::string>& EventLog::events() const {
    return this->m_events;
}

} // namespace aoc::ui
