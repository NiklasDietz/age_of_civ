#pragma once

/**
 * @file BitmapFont.hpp
 * @brief Vector-drawn monospace text using Renderer2D line primitives.
 *
 * Renders ASCII characters as simple line-based glyphs. No texture atlas
 * needed -- each character is drawn from a small set of line segments.
 * Sufficient for debug UI and prototype gameplay; can be replaced with
 * a proper texture-based font renderer later.
 */

#include "aoc/ui/Widget.hpp"

#include <cstdint>
#include <string_view>

namespace vulkan_app::renderer {
class Renderer2D;
}

namespace aoc::ui {

class BitmapFont {
public:
    /**
     * @brief Draw a text string at the given position.
     *
     * Characters are drawn left-to-right. Newlines are not handled.
     * Unsupported characters render as a small box.
     */
    static void drawText(vulkan_app::renderer::Renderer2D& renderer2d,
                          std::string_view text,
                          float x, float y,
                          float fontSize,
                          Color color);

    /**
     * @brief Measure the pixel dimensions of a text string.
     * @return Rect with w = total width, h = line height. x/y are zero.
     */
    [[nodiscard]] static Rect measureText(std::string_view text, float fontSize);

    /// Character width as fraction of fontSize.
    static constexpr float CHAR_WIDTH_RATIO  = 0.6f;
    /// Character spacing as fraction of fontSize.
    static constexpr float CHAR_SPACING_RATIO = 0.1f;

private:
    /// Draw a single character glyph using line segments.
    static void drawGlyph(vulkan_app::renderer::Renderer2D& renderer2d,
                           char ch, float x, float y, float size, Color color);
};

} // namespace aoc::ui
