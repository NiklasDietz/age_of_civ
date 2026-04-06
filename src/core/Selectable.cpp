/**
 * @file Selectable.cpp
 * @brief Selection visualization helper implementation.
 */

#include "aoc/core/Selectable.hpp"

#include <renderer/Renderer2D.hpp>

namespace aoc {

void drawSelectionHex(vulkan_app::renderer::Renderer2D& renderer2d,
                       float cx, float cy, float hexSize,
                       float r, float g, float b, float a,
                       float borderWidth) {
    const float hexW = hexSize * 0.866f;
    const float hexH = hexSize;
    renderer2d.drawHexagonOutline(cx, cy, hexW, hexH, r, g, b, a, borderWidth);
}

} // namespace aoc
