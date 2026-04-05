/**
 * @file BitmapFont.cpp
 * @brief TrueType font rendering via stb_truetype + Renderer2D filled rects.
 *
 * Rasterizes glyphs into small bitmaps using stb_truetype, then draws each
 * opaque pixel as a filled rectangle. Cached per (char, size) to avoid
 * re-rasterizing every frame.
 */

#include "aoc/ui/BitmapFont.hpp"
#include "aoc/core/Log.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <renderer/Renderer2D.hpp>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <vector>

namespace aoc::ui {

namespace {

/// Cached rasterized glyph bitmap.
struct GlyphBitmap {
    std::vector<uint8_t> pixels;
    int32_t width  = 0;
    int32_t height = 0;
    int32_t xOffset = 0;
    int32_t yOffset = 0;
    float   advance = 0.0f;
};

/// Global font state (initialized once).
struct FontState {
    bool initialized = false;
    std::vector<uint8_t> fontData;
    stbtt_fontinfo fontInfo{};

    /// Cache key: (codepoint << 8) | quantized_size
    std::unordered_map<uint32_t, GlyphBitmap> glyphCache;
};

FontState g_font;

uint32_t makeCacheKey(char ch, float fontSize) {
    uint32_t codepoint = static_cast<uint32_t>(static_cast<uint8_t>(ch));
    uint32_t quantizedSize = static_cast<uint32_t>(fontSize * 2.0f);  // 0.5px resolution
    return (codepoint << 16) | quantizedSize;
}

const GlyphBitmap& getGlyph(char ch, float fontSize) {
    uint32_t key = makeCacheKey(ch, fontSize);

    auto it = g_font.glyphCache.find(key);
    if (it != g_font.glyphCache.end()) {
        return it->second;
    }

    // Rasterize the glyph
    GlyphBitmap glyph;
    float scale = stbtt_ScaleForPixelHeight(&g_font.fontInfo, fontSize);

    int glyphIndex = stbtt_FindGlyphIndex(&g_font.fontInfo, static_cast<int>(static_cast<uint8_t>(ch)));
    if (glyphIndex == 0 && ch != ' ') {
        // Unknown character -- use '?' instead
        glyphIndex = stbtt_FindGlyphIndex(&g_font.fontInfo, '?');
    }

    int advanceWidth = 0;
    int leftBearing = 0;
    stbtt_GetGlyphHMetrics(&g_font.fontInfo, glyphIndex, &advanceWidth, &leftBearing);
    glyph.advance = static_cast<float>(advanceWidth) * scale;

    if (ch == ' ') {
        // Space has no bitmap
        glyph.width = 0;
        glyph.height = 0;
        g_font.glyphCache[key] = std::move(glyph);
        return g_font.glyphCache[key];
    }

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetGlyphBitmapBox(&g_font.fontInfo, glyphIndex, scale, scale, &x0, &y0, &x1, &y1);

    glyph.width  = x1 - x0;
    glyph.height = y1 - y0;
    glyph.xOffset = x0;
    glyph.yOffset = y0;

    if (glyph.width > 0 && glyph.height > 0) {
        glyph.pixels.resize(static_cast<std::size_t>(glyph.width) * static_cast<std::size_t>(glyph.height), 0);
        stbtt_MakeGlyphBitmap(&g_font.fontInfo, glyph.pixels.data(),
                               glyph.width, glyph.height, glyph.width,
                               scale, scale, glyphIndex);
    }

    g_font.glyphCache[key] = std::move(glyph);
    return g_font.glyphCache[key];
}

} // anonymous namespace

bool BitmapFont::initialize() {
    if (g_font.initialized) {
        return true;
    }

    // Try several common font paths
    const char* fontPaths[] = {
        "/usr/share/fonts/truetype/DejaVuSans.ttf",
        "/usr/share/fonts/texlive-dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/NotoSans-Regular.ttf",
    };

    std::ifstream file;
    const char* usedPath = nullptr;
    for (const char* path : fontPaths) {
        file.open(path, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            usedPath = path;
            break;
        }
    }

    if (!file.is_open()) {
        LOG_ERROR("BitmapFont: could not find any system TrueType font");
        return false;
    }

    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    g_font.fontData.resize(static_cast<std::size_t>(fileSize));
    file.read(reinterpret_cast<char*>(g_font.fontData.data()), fileSize);
    file.close();

    if (!stbtt_InitFont(&g_font.fontInfo, g_font.fontData.data(), 0)) {
        LOG_ERROR("BitmapFont: stbtt_InitFont failed for %s", usedPath);
        return false;
    }

    g_font.initialized = true;
    LOG_INFO("BitmapFont: loaded font from %s", usedPath);
    return true;
}

void BitmapFont::drawText(vulkan_app::renderer::Renderer2D& renderer2d,
                           std::string_view text,
                           float x, float y,
                           float fontSize,
                           Color color,
                           float pixelScale) {
    if (!g_font.initialized) {
        return;
    }

    // Rasterize at the screen-pixel font size for crisp glyphs,
    // then scale positions and rect sizes by pixelScale for world-space rendering.
    float rasterSize = fontSize / pixelScale;  // screen-pixel font size
    if (rasterSize < 4.0f) {
        rasterSize = 4.0f;
    }

    float baseline = y + fontSize * 0.75f;
    float cursorX = x;

    for (char ch : text) {
        const GlyphBitmap& glyph = getGlyph(ch, rasterSize);

        if (glyph.width > 0 && glyph.height > 0) {
            float glyphX = cursorX + static_cast<float>(glyph.xOffset) * pixelScale;
            float glyphY = baseline + static_cast<float>(glyph.yOffset) * pixelScale;

            for (int32_t py = 0; py < glyph.height; ++py) {
                for (int32_t px = 0; px < glyph.width; ++px) {
                    uint8_t alpha = glyph.pixels[static_cast<std::size_t>(py * glyph.width + px)];
                    if (alpha > 80) {
                        float pixelAlpha = static_cast<float>(alpha) / 255.0f * color.a;
                        renderer2d.drawFilledRect(
                            glyphX + static_cast<float>(px) * pixelScale,
                            glyphY + static_cast<float>(py) * pixelScale,
                            pixelScale, pixelScale,
                            color.r, color.g, color.b, pixelAlpha);
                    }
                }
            }
        }

        cursorX += glyph.advance * pixelScale;
    }
}

Rect BitmapFont::measureText(std::string_view text, float fontSize) {
    if (!g_font.initialized) {
        // Fallback estimation
        float advance = fontSize * (CHAR_WIDTH_RATIO + CHAR_SPACING_RATIO);
        return {0.0f, 0.0f, static_cast<float>(text.size()) * advance, fontSize};
    }

    float totalWidth = 0.0f;
    for (char ch : text) {
        const GlyphBitmap& glyph = getGlyph(ch, fontSize);
        totalWidth += glyph.advance;
    }

    return {0.0f, 0.0f, totalWidth, fontSize};
}

} // namespace aoc::ui
