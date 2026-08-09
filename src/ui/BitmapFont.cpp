/**
 * @file BitmapFont.cpp
 * @brief Text rendering from a pre-baked glyph atlas + Renderer2D filled rects.
 *
 * Draws each opaque glyph pixel as a filled rectangle, sampling coverage from
 * an atlas baked offline by `aoc_font_bake`.
 *
 * There is deliberately NO TrueType parser here. stb_truetype v1.26 has an
 * unpatched OOB read (CVE-2026-5314) reachable from stbtt_InitFont, so the
 * parser lives only in the offline baker and never ships in the game binary.
 * This loader reads a fixed-layout blob and bounds-checks every field, so a
 * truncated or corrupt atlas fails cleanly instead of reading out of bounds.
 */

#include "aoc/ui/BitmapFont.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/ui/FontAtlasFormat.hpp"
#include "aoc/ui/Theme.hpp"

#include <renderer/Renderer2D.hpp>

#include <cmath>
#include <cstring>
#include <fstream>
#include <vector>

namespace aoc::ui {

namespace {

struct FontState {
    bool initialized = false;
    /// The atlas is uploaded lazily on the first draw: initialize() runs before
    /// a Renderer2D exists, and this is the first point one is in hand.
    bool uploaded = false;

    uint32_t atlasWidth  = 0;
    uint32_t atlasHeight = 0;
    uint32_t sizeCount   = 0;

    std::vector<float> sizes;       ///< ascending pixel heights
    std::vector<AtlasGlyph> glyphs; ///< sizeCount * FONT_ATLAS_CHAR_COUNT, size-major
    std::vector<uint8_t> coverage;  ///< atlasWidth * atlasHeight
};

FontState g_font;

/// Nearest baked size index for a requested pixel height. Sizes are contiguous
/// integers, so this is a clamp plus a round -- no search.
uint32_t sizeIndexFor(float pixelHeight) {
    const float clamped = std::fmin(std::fmax(pixelHeight, static_cast<float>(FONT_ATLAS_MIN_SIZE)),
                                    static_cast<float>(FONT_ATLAS_MAX_SIZE));
    const float rounded = std::round(clamped) - static_cast<float>(FONT_ATLAS_MIN_SIZE);
    uint32_t index      = static_cast<uint32_t>(rounded < 0.0f ? 0.0f : rounded);
    if (index >= g_font.sizeCount) {
        index = g_font.sizeCount - 1;
    }
    return index;
}

/// Glyph record for `ch` at a baked size index. Characters outside printable
/// ASCII fall back to '?', matching the old rasterizer's behaviour.
const AtlasGlyph& glyphFor(char ch, uint32_t sizeIndex) {
    uint32_t code = static_cast<uint32_t>(static_cast<uint8_t>(ch));
    if (code < FONT_ATLAS_FIRST_CHAR || code >= FONT_ATLAS_FIRST_CHAR + FONT_ATLAS_CHAR_COUNT) {
        code = static_cast<uint32_t>('?');
    }
    const std::size_t offset = static_cast<std::size_t>(sizeIndex) * FONT_ATLAS_CHAR_COUNT +
                               (code - FONT_ATLAS_FIRST_CHAR);
    return g_font.glyphs[offset];
}

/// Read exactly `count` objects, failing if the stream is short. Every load-time
/// read goes through this so a truncated file cannot leave partly-filled state.
template <typename T> bool readExact(std::ifstream& file, T* dest, std::size_t count) {
    file.read(reinterpret_cast<char*>(dest), static_cast<std::streamsize>(sizeof(T) * count));
    return file.gcount() == static_cast<std::streamsize>(sizeof(T) * count);
}

} // anonymous namespace

bool BitmapFont::initialize() {
    if (g_font.initialized) {
        return true;
    }

    // Baked next to the other runtime data by the aoc_font_bake build step.
    const char* atlasPath = "data/fonts/ui_font.aocfnt";

    std::ifstream file(atlasPath, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("BitmapFont: cannot open glyph atlas '%s'", atlasPath);
        return false;
    }

    AtlasHeader header{};
    if (!readExact(file, &header, 1)) {
        LOG_ERROR("BitmapFont: glyph atlas '%s' is truncated (header)", atlasPath);
        return false;
    }
    if (header.magic != FONT_ATLAS_MAGIC) {
        LOG_ERROR("BitmapFont: glyph atlas '%s' has bad magic 0x%08X", atlasPath, header.magic);
        return false;
    }
    if (header.version != FONT_ATLAS_VERSION) {
        LOG_ERROR("BitmapFont: glyph atlas '%s' is version %u, expected %u", atlasPath,
                  header.version, FONT_ATLAS_VERSION);
        return false;
    }
    if (header.charCount != FONT_ATLAS_CHAR_COUNT || header.firstChar != FONT_ATLAS_FIRST_CHAR) {
        LOG_ERROR("BitmapFont: glyph atlas '%s' character range does not match this build",
                  atlasPath);
        return false;
    }

    // A fontless bake writes a header-only atlas on purpose; treat it as "no
    // font" so measureText's estimation path takes over, as it did before.
    if (header.sizeCount == 0) {
        LOG_ERROR("BitmapFont: glyph atlas '%s' is empty (font missing at bake time)", atlasPath);
        return false;
    }
    if (header.sizeCount != FONT_ATLAS_SIZE_COUNT) {
        LOG_ERROR("BitmapFont: glyph atlas '%s' has %u sizes, expected %u", atlasPath,
                  header.sizeCount, FONT_ATLAS_SIZE_COUNT);
        return false;
    }

    const std::size_t coverageBytes =
        static_cast<std::size_t>(header.atlasWidth) * static_cast<std::size_t>(header.atlasHeight);
    if (coverageBytes == 0) {
        LOG_ERROR("BitmapFont: glyph atlas '%s' has zero-sized bitmap", atlasPath);
        return false;
    }

    g_font.sizes.resize(header.sizeCount);
    for (uint32_t i = 0; i < header.sizeCount; ++i) {
        AtlasSizeEntry entry{};
        if (!readExact(file, &entry, 1)) {
            LOG_ERROR("BitmapFont: glyph atlas '%s' is truncated (size table)", atlasPath);
            return false;
        }
        g_font.sizes[i] = entry.pixelHeight;
    }

    const std::size_t glyphCount =
        static_cast<std::size_t>(header.sizeCount) * FONT_ATLAS_CHAR_COUNT;
    g_font.glyphs.resize(glyphCount);
    if (!readExact(file, g_font.glyphs.data(), glyphCount)) {
        LOG_ERROR("BitmapFont: glyph atlas '%s' is truncated (glyph table)", atlasPath);
        return false;
    }

    g_font.coverage.resize(coverageBytes);
    if (!readExact(file, g_font.coverage.data(), coverageBytes)) {
        LOG_ERROR("BitmapFont: glyph atlas '%s' is truncated (bitmap)", atlasPath);
        return false;
    }

    // Every glyph rect must lie inside the bitmap. Validating once here is what
    // lets the draw loop index the coverage array without per-pixel checks.
    for (const AtlasGlyph& glyph : g_font.glyphs) {
        if (glyph.x1 > header.atlasWidth || glyph.y1 > header.atlasHeight || glyph.x0 > glyph.x1 ||
            glyph.y0 > glyph.y1) {
            LOG_ERROR("BitmapFont: glyph atlas '%s' has an out-of-range glyph rect", atlasPath);
            return false;
        }
    }

    g_font.atlasWidth  = header.atlasWidth;
    g_font.atlasHeight = header.atlasHeight;
    g_font.sizeCount   = header.sizeCount;
    g_font.initialized = true;

    LOG_INFO("BitmapFont: loaded glyph atlas %s (%ux%u, %u sizes)", atlasPath, header.atlasWidth,
             header.atlasHeight, header.sizeCount);
    return true;
}

void BitmapFont::drawText(vulkan_app::renderer::Renderer2D& renderer2d, std::string_view text,
                          float x, float y, float fontSize, Color color, float pixelScale) {
    if (!g_font.initialized) {
        return;
    }

    if (!g_font.uploaded) {
        // One-shot: hand the coverage bitmap to the renderer, then drop our
        // copy -- the GPU owns it from here and nothing samples it CPU-side.
        g_font.uploaded = renderer2d.setCoverageAtlas(g_font.coverage.data(), g_font.atlasWidth,
                                                      g_font.atlasHeight);
        if (!g_font.uploaded) {
            LOG_ERROR("BitmapFont: coverage atlas upload failed; text will not render");
            return;
        }
        std::vector<uint8_t>().swap(g_font.coverage);
    }

    // Global readability scale (UI-scale slider x resolution). Applied here so
    // every call site scales consistently; measureText applies the identical
    // factor so layout and hit-boxes stay in agreement.
    fontSize *= theme().fontScale();

    // Sample the atlas at the screen-pixel font size for crisp glyphs, then
    // scale positions and rect sizes by pixelScale for world-space rendering.
    const uint32_t sizeIndex = sizeIndexFor(fontSize / pixelScale);

    float baseline = y + fontSize * 0.75f;
    float cursorX  = x;

    for (char ch : text) {
        const AtlasGlyph& glyph = glyphFor(ch, sizeIndex);
        const int32_t gw        = static_cast<int32_t>(glyph.x1) - static_cast<int32_t>(glyph.x0);
        const int32_t gh        = static_cast<int32_t>(glyph.y1) - static_cast<int32_t>(glyph.y0);

        if (gw > 0 && gh > 0) {
            const float glyphX = cursorX + glyph.xOffset * pixelScale;
            const float glyphY = baseline + glyph.yOffset * pixelScale;

            // One instance per glyph. This used to emit one filled rect per
            // opaque pixel -- ~60x more instances for the same text, and the
            // reason an 8-direction label outline was once worth deleting on
            // cost grounds. The shader samples coverage directly, so the edge
            // ramp survives without the old alpha cutoff.
            const float atlasW = static_cast<float>(g_font.atlasWidth);
            const float atlasH = static_cast<float>(g_font.atlasHeight);
            renderer2d.drawGlyph(
                glyphX, glyphY, static_cast<float>(gw) * pixelScale,
                static_cast<float>(gh) * pixelScale, static_cast<float>(glyph.x0) / atlasW,
                static_cast<float>(glyph.y0) / atlasH, static_cast<float>(glyph.x1) / atlasW,
                static_cast<float>(glyph.y1) / atlasH, color.r, color.g, color.b, color.a);
        }

        cursorX += glyph.xAdvance * pixelScale;
    }
}

Rect BitmapFont::measureText(std::string_view text, float fontSize) {
    // Same factor drawText applies — must stay in lockstep or centring drifts.
    fontSize *= theme().fontScale();

    if (!g_font.initialized) {
        // Fallback estimation
        float advance = fontSize * (CHAR_WIDTH_RATIO + CHAR_SPACING_RATIO);
        return {0.0f, 0.0f, static_cast<float>(text.size()) * advance, fontSize};
    }

    // Must resolve the same baked size drawText will, or measured widths and
    // drawn widths diverge and every centred label drifts.
    const uint32_t sizeIndex = sizeIndexFor(fontSize);

    float totalWidth = 0.0f;
    for (char ch : text) {
        totalWidth += glyphFor(ch, sizeIndex).xAdvance;
    }

    return {0.0f, 0.0f, totalWidth, fontSize};
}

} // namespace aoc::ui
