/**
 * @file WidgetInspector.cpp
 */

#include "aoc/ui/WidgetInspector.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/BitmapFont.hpp"
#include "aoc/ui/StyleTokens.hpp"

#include <renderer/Renderer2D.hpp>

#include <cstdio>
#include <string>

namespace aoc::ui {

std::string WidgetInspector::dump(const UIManager& ui) {
    return ui.dumpTreeJson();
}

void WidgetInspector::render(vulkan_app::renderer::Renderer2D& renderer,
                             const UIManager& ui,
                             float mouseX, float mouseY) const {
    if (!this->m_enabled) { return; }

    // Highlight the hovered widget bounds with a thin red outline.
    const WidgetId hovered = ui.hoveredWidget();
    if (hovered != INVALID_WIDGET) {
        const Widget* w = ui.getWidget(hovered);
        if (w != nullptr) {
            const Rect& b = w->computedBounds;
            const float thick = 2.0f;
            renderer.drawFilledRect(b.x, b.y, b.w, thick, tokens::DIPLO_HOSTILE.r, tokens::DIPLO_HOSTILE.g, tokens::DIPLO_HOSTILE.b, 0.9f);
            renderer.drawFilledRect(b.x, b.y + b.h - thick, b.w, thick,
                                     tokens::DIPLO_HOSTILE.r, tokens::DIPLO_HOSTILE.g, tokens::DIPLO_HOSTILE.b, 0.9f);
            renderer.drawFilledRect(b.x, b.y, thick, b.h, tokens::DIPLO_HOSTILE.r, tokens::DIPLO_HOSTILE.g, tokens::DIPLO_HOSTILE.b, 0.9f);
            renderer.drawFilledRect(b.x + b.w - thick, b.y, thick, b.h,
                                     tokens::DIPLO_HOSTILE.r, tokens::DIPLO_HOSTILE.g, tokens::DIPLO_HOSTILE.b, 0.9f);
        }
    }

    // Stats box in the top-left. Keeps text-only to avoid tangling
    // with the inspector's target.
    char line[128];
    std::snprintf(line, sizeof(line),
                  "[INSPECTOR] hovered=%u focused=%u mouse=(%.0f,%.0f)",
                  static_cast<unsigned>(hovered),
                  static_cast<unsigned>(ui.focusedWidget()),
                  static_cast<double>(mouseX),
                  static_cast<double>(mouseY));
    renderer.drawFilledRect(4.0f, 4.0f, 520.0f, 20.0f,
                             tokens::SURFACE_INK.r, tokens::SURFACE_INK.g,
                             tokens::SURFACE_INK.b, 0.75f);
    BitmapFont::drawText(renderer, line, 8.0f, 6.0f, 12.0f,
                          tokens::TEXT_GILT, 1.0f);
}

} // namespace aoc::ui
