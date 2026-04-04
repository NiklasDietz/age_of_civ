/**
 * @file BitmapFont.cpp
 * @brief Line-segment based character rendering for ASCII glyphs.
 *
 * Each character is defined as a set of line segments in a normalized
 * coordinate space [0,1] x [0,1], then scaled to the requested fontSize.
 * This is intentionally simple and ugly -- it works without any external
 * font files or texture loading, which is exactly what we need for Phase 3.
 */

#include "aoc/ui/BitmapFont.hpp"

#include <renderer/Renderer2D.hpp>

#include <array>
#include <cstddef>

namespace aoc::ui {

namespace {

/// A line segment in normalized [0,1] space.
struct Seg {
    float x1, y1, x2, y2;
};

/// Max segments per glyph.
static constexpr int MAX_SEGS = 7;

struct GlyphDef {
    int segCount;
    std::array<Seg, MAX_SEGS> segs;
};

// Glyph definitions for printable ASCII 32-127.
// Each glyph fits in a box: x=[0, 0.6], y=[0, 1.0] (aspect ~0.6:1).
// Coordinates: (0,0) = top-left, (0.6, 1.0) = bottom-right.
// Only commonly needed characters are fully defined; others get a box.

constexpr GlyphDef UNKNOWN_GLYPH = {4, {{
    {0.0f,0.0f, 0.5f,0.0f}, {0.5f,0.0f, 0.5f,1.0f},
    {0.5f,1.0f, 0.0f,1.0f}, {0.0f,1.0f, 0.0f,0.0f},
    {},{},{}
}}};

// Helper macros would make this shorter but less readable.
// Each letter is hand-specified with up to 7 segments.

constexpr GlyphDef glyphFor(char ch) {
    switch (ch) {
        case ' ': return {0, {}};
        case '0': return {6, {{{0.0f,0.0f, 0.5f,0.0f}, {0.5f,0.0f, 0.5f,1.0f}, {0.5f,1.0f, 0.0f,1.0f}, {0.0f,1.0f, 0.0f,0.0f}, {0.0f,0.0f, 0.5f,1.0f}, {}, {}}}};
        case '1': return {3, {{{0.25f,0.0f, 0.25f,1.0f}, {0.1f,0.2f, 0.25f,0.0f}, {0.0f,1.0f, 0.5f,1.0f}, {}, {}, {}, {}}}};
        case '2': return {5, {{{0.0f,0.0f, 0.5f,0.0f}, {0.5f,0.0f, 0.5f,0.5f}, {0.5f,0.5f, 0.0f,0.5f}, {0.0f,0.5f, 0.0f,1.0f}, {0.0f,1.0f, 0.5f,1.0f}, {}, {}}}};
        case '3': return {4, {{{0.0f,0.0f, 0.5f,0.0f}, {0.5f,0.0f, 0.5f,1.0f}, {0.5f,1.0f, 0.0f,1.0f}, {0.0f,0.5f, 0.5f,0.5f}, {}, {}, {}}}};
        case '4': return {3, {{{0.0f,0.0f, 0.0f,0.5f}, {0.0f,0.5f, 0.5f,0.5f}, {0.5f,0.0f, 0.5f,1.0f}, {}, {}, {}, {}}}};
        case '5': return {5, {{{0.5f,0.0f, 0.0f,0.0f}, {0.0f,0.0f, 0.0f,0.5f}, {0.0f,0.5f, 0.5f,0.5f}, {0.5f,0.5f, 0.5f,1.0f}, {0.5f,1.0f, 0.0f,1.0f}, {}, {}}}};
        case '6': return {5, {{{0.5f,0.0f, 0.0f,0.0f}, {0.0f,0.0f, 0.0f,1.0f}, {0.0f,1.0f, 0.5f,1.0f}, {0.5f,1.0f, 0.5f,0.5f}, {0.5f,0.5f, 0.0f,0.5f}, {}, {}}}};
        case '7': return {2, {{{0.0f,0.0f, 0.5f,0.0f}, {0.5f,0.0f, 0.15f,1.0f}, {}, {}, {}, {}, {}}}};
        case '8': return {7, {{{0.0f,0.0f, 0.5f,0.0f}, {0.5f,0.0f, 0.5f,0.5f}, {0.5f,0.5f, 0.0f,0.5f}, {0.0f,0.5f, 0.0f,0.0f}, {0.0f,0.5f, 0.0f,1.0f}, {0.0f,1.0f, 0.5f,1.0f}, {0.5f,1.0f, 0.5f,0.5f}}}};
        case '9': return {5, {{{0.0f,1.0f, 0.5f,1.0f}, {0.5f,1.0f, 0.5f,0.0f}, {0.5f,0.0f, 0.0f,0.0f}, {0.0f,0.0f, 0.0f,0.5f}, {0.0f,0.5f, 0.5f,0.5f}, {}, {}}}};

        case 'A': case 'a': return {3, {{{0.0f,1.0f, 0.25f,0.0f}, {0.25f,0.0f, 0.5f,1.0f}, {0.1f,0.6f, 0.4f,0.6f}, {}, {}, {}, {}}}};
        case 'B': case 'b': return {7, {{{0.0f,0.0f, 0.0f,1.0f}, {0.0f,0.0f, 0.4f,0.0f}, {0.4f,0.0f, 0.5f,0.15f}, {0.5f,0.15f, 0.4f,0.5f}, {0.4f,0.5f, 0.0f,0.5f}, {0.4f,0.5f, 0.5f,0.75f}, {0.5f,0.75f, 0.0f,1.0f}}}};
        case 'C': case 'c': return {3, {{{0.5f,0.0f, 0.0f,0.0f}, {0.0f,0.0f, 0.0f,1.0f}, {0.0f,1.0f, 0.5f,1.0f}, {}, {}, {}, {}}}};
        case 'D': case 'd': return {4, {{{0.0f,0.0f, 0.0f,1.0f}, {0.0f,0.0f, 0.35f,0.0f}, {0.35f,0.0f, 0.5f,0.5f}, {0.5f,0.5f, 0.0f,1.0f}, {}, {}, {}}}};
        case 'E': case 'e': return {4, {{{0.5f,0.0f, 0.0f,0.0f}, {0.0f,0.0f, 0.0f,1.0f}, {0.0f,1.0f, 0.5f,1.0f}, {0.0f,0.5f, 0.35f,0.5f}, {}, {}, {}}}};
        case 'F': case 'f': return {3, {{{0.5f,0.0f, 0.0f,0.0f}, {0.0f,0.0f, 0.0f,1.0f}, {0.0f,0.5f, 0.35f,0.5f}, {}, {}, {}, {}}}};
        case 'G': case 'g': return {5, {{{0.5f,0.0f, 0.0f,0.0f}, {0.0f,0.0f, 0.0f,1.0f}, {0.0f,1.0f, 0.5f,1.0f}, {0.5f,1.0f, 0.5f,0.5f}, {0.5f,0.5f, 0.3f,0.5f}, {}, {}}}};
        case 'H': case 'h': return {3, {{{0.0f,0.0f, 0.0f,1.0f}, {0.5f,0.0f, 0.5f,1.0f}, {0.0f,0.5f, 0.5f,0.5f}, {}, {}, {}, {}}}};
        case 'I': case 'i': return {3, {{{0.1f,0.0f, 0.4f,0.0f}, {0.25f,0.0f, 0.25f,1.0f}, {0.1f,1.0f, 0.4f,1.0f}, {}, {}, {}, {}}}};
        case 'J': case 'j': return {3, {{{0.1f,0.0f, 0.5f,0.0f}, {0.35f,0.0f, 0.35f,1.0f}, {0.35f,1.0f, 0.0f,0.8f}, {}, {}, {}, {}}}};
        case 'K': case 'k': return {3, {{{0.0f,0.0f, 0.0f,1.0f}, {0.5f,0.0f, 0.0f,0.5f}, {0.0f,0.5f, 0.5f,1.0f}, {}, {}, {}, {}}}};
        case 'L': case 'l': return {2, {{{0.0f,0.0f, 0.0f,1.0f}, {0.0f,1.0f, 0.5f,1.0f}, {}, {}, {}, {}, {}}}};
        case 'M': case 'm': return {4, {{{0.0f,1.0f, 0.0f,0.0f}, {0.0f,0.0f, 0.25f,0.5f}, {0.25f,0.5f, 0.5f,0.0f}, {0.5f,0.0f, 0.5f,1.0f}, {}, {}, {}}}};
        case 'N': case 'n': return {3, {{{0.0f,1.0f, 0.0f,0.0f}, {0.0f,0.0f, 0.5f,1.0f}, {0.5f,1.0f, 0.5f,0.0f}, {}, {}, {}, {}}}};
        case 'O': case 'o': return {4, {{{0.0f,0.0f, 0.5f,0.0f}, {0.5f,0.0f, 0.5f,1.0f}, {0.5f,1.0f, 0.0f,1.0f}, {0.0f,1.0f, 0.0f,0.0f}, {}, {}, {}}}};
        case 'P': case 'p': return {4, {{{0.0f,0.0f, 0.0f,1.0f}, {0.0f,0.0f, 0.5f,0.0f}, {0.5f,0.0f, 0.5f,0.5f}, {0.5f,0.5f, 0.0f,0.5f}, {}, {}, {}}}};
        case 'Q': case 'q': return {5, {{{0.0f,0.0f, 0.5f,0.0f}, {0.5f,0.0f, 0.5f,1.0f}, {0.5f,1.0f, 0.0f,1.0f}, {0.0f,1.0f, 0.0f,0.0f}, {0.35f,0.75f, 0.55f,1.05f}, {}, {}}}};
        case 'R': case 'r': return {5, {{{0.0f,0.0f, 0.0f,1.0f}, {0.0f,0.0f, 0.5f,0.0f}, {0.5f,0.0f, 0.5f,0.5f}, {0.5f,0.5f, 0.0f,0.5f}, {0.25f,0.5f, 0.5f,1.0f}, {}, {}}}};
        case 'S': case 's': return {5, {{{0.5f,0.0f, 0.0f,0.0f}, {0.0f,0.0f, 0.0f,0.5f}, {0.0f,0.5f, 0.5f,0.5f}, {0.5f,0.5f, 0.5f,1.0f}, {0.5f,1.0f, 0.0f,1.0f}, {}, {}}}};
        case 'T': case 't': return {2, {{{0.0f,0.0f, 0.5f,0.0f}, {0.25f,0.0f, 0.25f,1.0f}, {}, {}, {}, {}, {}}}};
        case 'U': case 'u': return {3, {{{0.0f,0.0f, 0.0f,1.0f}, {0.0f,1.0f, 0.5f,1.0f}, {0.5f,1.0f, 0.5f,0.0f}, {}, {}, {}, {}}}};
        case 'V': case 'v': return {2, {{{0.0f,0.0f, 0.25f,1.0f}, {0.25f,1.0f, 0.5f,0.0f}, {}, {}, {}, {}, {}}}};
        case 'W': case 'w': return {4, {{{0.0f,0.0f, 0.1f,1.0f}, {0.1f,1.0f, 0.25f,0.5f}, {0.25f,0.5f, 0.4f,1.0f}, {0.4f,1.0f, 0.5f,0.0f}, {}, {}, {}}}};
        case 'X': case 'x': return {2, {{{0.0f,0.0f, 0.5f,1.0f}, {0.5f,0.0f, 0.0f,1.0f}, {}, {}, {}, {}, {}}}};
        case 'Y': case 'y': return {3, {{{0.0f,0.0f, 0.25f,0.5f}, {0.5f,0.0f, 0.25f,0.5f}, {0.25f,0.5f, 0.25f,1.0f}, {}, {}, {}, {}}}};
        case 'Z': case 'z': return {3, {{{0.0f,0.0f, 0.5f,0.0f}, {0.5f,0.0f, 0.0f,1.0f}, {0.0f,1.0f, 0.5f,1.0f}, {}, {}, {}, {}}}};

        case '-': return {1, {{{0.1f,0.5f, 0.4f,0.5f}, {}, {}, {}, {}, {}, {}}}};
        case '+': return {2, {{{0.1f,0.5f, 0.4f,0.5f}, {0.25f,0.3f, 0.25f,0.7f}, {}, {}, {}, {}, {}}}};
        case '.': return {1, {{{0.2f,0.9f, 0.3f,0.9f}, {}, {}, {}, {}, {}, {}}}};
        case ',': return {1, {{{0.2f,0.9f, 0.15f,1.1f}, {}, {}, {}, {}, {}, {}}}};
        case ':': return {2, {{{0.22f,0.3f, 0.28f,0.3f}, {0.22f,0.7f, 0.28f,0.7f}, {}, {}, {}, {}, {}}}};
        case '!': return {2, {{{0.25f,0.0f, 0.25f,0.7f}, {0.22f,0.9f, 0.28f,0.9f}, {}, {}, {}, {}, {}}}};
        case '?': return {4, {{{0.0f,0.1f, 0.25f,0.0f}, {0.25f,0.0f, 0.5f,0.2f}, {0.5f,0.2f, 0.25f,0.5f}, {0.22f,0.9f, 0.28f,0.9f}, {}, {}, {}}}};
        case '(': return {1, {{{0.3f,0.0f, 0.15f,0.5f}, {}, {}, {}, {}, {}, {}}}};  // simplified
        case ')': return {1, {{{0.2f,0.0f, 0.35f,0.5f}, {}, {}, {}, {}, {}, {}}}};
        case '/': return {1, {{{0.5f,0.0f, 0.0f,1.0f}, {}, {}, {}, {}, {}, {}}}};
        case '%': return {3, {{{0.5f,0.0f, 0.0f,1.0f}, {0.05f,0.1f, 0.15f,0.1f}, {0.35f,0.9f, 0.45f,0.9f}, {}, {}, {}, {}}}};

        default: return UNKNOWN_GLYPH;
    }
}

} // anonymous namespace

void BitmapFont::drawText(vulkan_app::renderer::Renderer2D& renderer2d,
                           std::string_view text,
                           float x, float y,
                           float fontSize,
                           Color color) {
    float advance = fontSize * (CHAR_WIDTH_RATIO + CHAR_SPACING_RATIO);

    float cursorX = x;
    for (char ch : text) {
        drawGlyph(renderer2d, ch, cursorX, y, fontSize, color);
        cursorX += advance;
    }
}

Rect BitmapFont::measureText(std::string_view text, float fontSize) {
    float advance = fontSize * (CHAR_WIDTH_RATIO + CHAR_SPACING_RATIO);
    float totalWidth = static_cast<float>(text.size()) * advance;
    if (!text.empty()) {
        // Remove trailing spacing
        totalWidth -= fontSize * CHAR_SPACING_RATIO;
    }
    return {0.0f, 0.0f, totalWidth, fontSize};
}

void BitmapFont::drawGlyph(vulkan_app::renderer::Renderer2D& renderer2d,
                             char ch, float x, float y, float size, Color color) {
    if (ch == ' ') {
        return;
    }

    GlyphDef glyph = glyphFor(ch);
    float scaleX = size * CHAR_WIDTH_RATIO;
    float scaleY = size;
    float thickness = size * 0.1f;
    if (thickness < 1.0f) {
        thickness = 1.0f;
    }

    for (int i = 0; i < glyph.segCount; ++i) {
        const Seg& seg = glyph.segs[static_cast<std::size_t>(i)];
        renderer2d.drawLine(
            x + seg.x1 * scaleX, y + seg.y1 * scaleY,
            x + seg.x2 * scaleX, y + seg.y2 * scaleY,
            thickness,
            color.r, color.g, color.b, color.a);
    }
}

} // namespace aoc::ui
