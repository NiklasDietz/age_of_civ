#pragma once

/**
 * @file BitmapFont.hpp
 * @brief TrueType font rendering using stb_truetype rasterization and Renderer2D filled rects.
 *
 * Loads a system TrueType font (DejaVu Sans), rasterizes glyphs into bitmaps
 * on demand, and draws each opaque pixel as a small filled rectangle via
 * the Renderer2D primitive API. Simple but produces readable text at all sizes.
 */

#include "aoc/ui/Widget.hpp"

#include <string_view>

namespace vulkan_app::renderer {
class Renderer2D;
}

namespace aoc::ui {

class BitmapFont {
public:
    /**
     * @brief Initialize the font system. Call once at startup.
     *
     * Loads the TrueType font file from the system fonts directory.
     * @return true if font loaded successfully.
     */
    [[nodiscard]] static bool initialize();

    /**
     * @brief Draw a text string at the given position.
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

    /// Character advance width as fraction of fontSize (for measureText consistency).
    static constexpr float CHAR_WIDTH_RATIO  = 0.55f;
    static constexpr float CHAR_SPACING_RATIO = 0.05f;
};

} // namespace aoc::ui
