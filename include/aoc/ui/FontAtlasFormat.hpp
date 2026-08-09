#pragma once

/**
 * @file FontAtlasFormat.hpp
 * @brief On-disk layout of the pre-baked glyph atlas (.aocfnt).
 *
 * Shared by the offline baker (`aoc_font_bake`, the only target that links
 * stb_truetype) and the runtime loader in BitmapFont.cpp. Keeping the record
 * layout in one header is what stops the two from drifting.
 *
 * Why this exists: stb_truetype v1.26 carries an unpatched OOB read
 * (CVE-2026-5314) reachable from stbtt_InitFont on a malformed font. Baking
 * offline moves the font parser out of the shipped binary entirely, so the
 * game never parses a TrueType file at runtime -- it reads this fixed-layout
 * blob instead, with every field bounds-checked on load.
 *
 * Layout (little-endian, tightly packed in this order):
 *
 *   AtlasHeader
 *   AtlasSizeEntry   x header.sizeCount        -- ascending pixelHeight
 *   AtlasGlyph       x sizeCount * charCount   -- size-major, then codepoint
 *   uint8_t          x atlasWidth * atlasHeight -- 8-bit coverage
 */

#include <cstdint>

namespace aoc::ui {

/// "AOCF" — checked on load; a mismatch is a hard failure, not a fallback.
inline constexpr uint32_t FONT_ATLAS_MAGIC   = 0x46434F41u;
inline constexpr uint32_t FONT_ATLAS_VERSION = 1u;

/// Printable ASCII. Anything outside renders as '?' (see BitmapFont).
inline constexpr uint32_t FONT_ATLAS_FIRST_CHAR = 32u; // space
inline constexpr uint32_t FONT_ATLAS_CHAR_COUNT = 95u; // 32..126 inclusive

/// Baked pixel heights. Runtime rounds its requested size to the nearest entry
/// and uses that entry's metrics for BOTH drawing and measuring, so layout
/// stays self-consistent even though the size is quantized to 1 px.
inline constexpr uint32_t FONT_ATLAS_MIN_SIZE   = 6u;
inline constexpr uint32_t FONT_ATLAS_MAX_SIZE   = 48u;
inline constexpr uint32_t FONT_ATLAS_SIZE_COUNT = FONT_ATLAS_MAX_SIZE - FONT_ATLAS_MIN_SIZE + 1u;

struct AtlasHeader {
    uint32_t magic       = FONT_ATLAS_MAGIC;
    uint32_t version     = FONT_ATLAS_VERSION;
    uint32_t atlasWidth  = 0;
    uint32_t atlasHeight = 0;
    uint32_t sizeCount   = 0;
    uint32_t firstChar   = FONT_ATLAS_FIRST_CHAR;
    uint32_t charCount   = FONT_ATLAS_CHAR_COUNT;
};

struct AtlasSizeEntry {
    float pixelHeight = 0.0f;
};

/// One glyph at one size. Rect is in atlas pixels; offsets and advance are in
/// screen pixels relative to the pen position, matching stb_truetype's
/// packedchar semantics so the draw path is unchanged.
struct AtlasGlyph {
    uint16_t x0    = 0;
    uint16_t y0    = 0;
    uint16_t x1    = 0;
    uint16_t y1    = 0;
    float xOffset  = 0.0f;
    float yOffset  = 0.0f;
    float xAdvance = 0.0f;
};

} // namespace aoc::ui
