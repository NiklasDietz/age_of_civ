/**
 * @file FontBake.cpp
 * @brief Offline glyph-atlas baker. The ONLY target that links stb_truetype.
 *
 * Packs printable ASCII at every integer pixel height in
 * [FONT_ATLAS_MIN_SIZE, FONT_ATLAS_MAX_SIZE] into a single 8-bit coverage
 * atlas and writes it plus a metrics table as a .aocfnt blob.
 *
 * Run at build time (see the aoc_font_bake custom command in CMakeLists.txt)
 * so the shipped game binary contains no TrueType parser at all -- that is the
 * point of this tool, not merely a build convenience. See FontAtlasFormat.hpp.
 *
 *   aoc_font_bake <output.aocfnt> [font.ttf]
 *
 * With no font argument, probes the same trusted system paths BitmapFont used
 * to. Missing font is NOT a build failure: it writes a valid empty atlas and
 * the runtime falls back to estimated metrics, matching the old behaviour when
 * no system font was present.
 */

#include "aoc/ui/FontAtlasFormat.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

/// Same trusted-path list BitmapFont::initialize used. Kept fixed on purpose:
/// this parser must never see a path from a mod, save, or other input.
const char* const kFontPaths[] = {
    "/usr/share/fonts/truetype/DejaVuSans.ttf",
    "/usr/share/fonts/texlive-dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/LiberationSans-Regular.ttf",
    "/usr/share/fonts/truetype/NotoSans-Regular.ttf",
};

/// Sized to hold every baked size of printable ASCII for DejaVuSans with room
/// for packing waste. If a future font or size range overflows it, PackFontRange
/// fails and the build errors rather than silently dropping glyphs.
constexpr int kAtlasWidth  = 2048;
constexpr int kAtlasHeight = 2048;

bool readFile(const char* path, std::vector<uint8_t>& out) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }
    const std::streamsize size = file.tellg();
    if (size <= 0) {
        return false;
    }
    file.seekg(0, std::ios::beg);
    out.resize(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(out.data()), size);
    return file.good() || file.eof();
}

/// Write a header-only atlas so a fontless build still produces a valid file.
bool writeEmptyAtlas(const char* outPath) {
    aoc::ui::AtlasHeader header{};
    header.atlasWidth  = 0;
    header.atlasHeight = 0;
    header.sizeCount   = 0;

    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        std::fprintf(stderr, "aoc_font_bake: cannot open '%s' for writing\n", outPath);
        return false;
    }
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    return out.good();
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: aoc_font_bake <output.aocfnt> [font.ttf]\n");
        return 1;
    }
    const char* outPath = argv[1];

    std::vector<uint8_t> fontData;
    const char* usedPath = nullptr;
    if (argc >= 3) {
        if (!readFile(argv[2], fontData)) {
            std::fprintf(stderr, "aoc_font_bake: cannot read font '%s'\n", argv[2]);
            return 1;
        }
        usedPath = argv[2];
    } else {
        for (const char* path : kFontPaths) {
            if (readFile(path, fontData)) {
                usedPath = path;
                break;
            }
        }
    }

    if (usedPath == nullptr) {
        std::fprintf(stderr, "aoc_font_bake: no system TrueType font found; writing empty atlas. "
                             "Text will fall back to estimated metrics.\n");
        return writeEmptyAtlas(outPath) ? 0 : 1;
    }

    std::vector<uint8_t> atlas(static_cast<std::size_t>(kAtlasWidth) *
                               static_cast<std::size_t>(kAtlasHeight));

    stbtt_pack_context packContext{};
    if (stbtt_PackBegin(&packContext, atlas.data(), kAtlasWidth, kAtlasHeight, kAtlasWidth, 1,
                        nullptr) == 0) {
        std::fprintf(stderr, "aoc_font_bake: stbtt_PackBegin failed\n");
        return 1;
    }
    // Matches the runtime's per-pixel coverage sampling; no hinting so the
    // baked bitmaps are identical to what the old runtime rasterizer produced.
    stbtt_PackSetOversampling(&packContext, 1, 1);

    const uint32_t sizeCount = aoc::ui::FONT_ATLAS_SIZE_COUNT;
    const uint32_t charCount = aoc::ui::FONT_ATLAS_CHAR_COUNT;

    std::vector<stbtt_packedchar> packed(static_cast<std::size_t>(sizeCount) *
                                         static_cast<std::size_t>(charCount));

    for (uint32_t i = 0; i < sizeCount; ++i) {
        const float pixelHeight = static_cast<float>(aoc::ui::FONT_ATLAS_MIN_SIZE + i);
        stbtt_pack_range range{};
        range.font_size                        = pixelHeight;
        range.first_unicode_codepoint_in_range = static_cast<int>(aoc::ui::FONT_ATLAS_FIRST_CHAR);
        range.num_chars                        = static_cast<int>(charCount);
        range.chardata_for_range = packed.data() + static_cast<std::size_t>(i) * charCount;

        if (stbtt_PackFontRanges(&packContext, fontData.data(), 0, &range, 1) == 0) {
            std::fprintf(stderr,
                         "aoc_font_bake: atlas full at %.0f px -- raise kAtlasWidth/Height or "
                         "narrow the size range\n",
                         static_cast<double>(pixelHeight));
            stbtt_PackEnd(&packContext);
            return 1;
        }
    }
    stbtt_PackEnd(&packContext);

    aoc::ui::AtlasHeader header{};
    header.atlasWidth  = kAtlasWidth;
    header.atlasHeight = kAtlasHeight;
    header.sizeCount   = sizeCount;

    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        std::fprintf(stderr, "aoc_font_bake: cannot open '%s' for writing\n", outPath);
        return 1;
    }
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));

    for (uint32_t i = 0; i < sizeCount; ++i) {
        aoc::ui::AtlasSizeEntry entry{};
        entry.pixelHeight = static_cast<float>(aoc::ui::FONT_ATLAS_MIN_SIZE + i);
        out.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
    }

    for (const stbtt_packedchar& pc : packed) {
        aoc::ui::AtlasGlyph glyph{};
        glyph.x0       = pc.x0;
        glyph.y0       = pc.y0;
        glyph.x1       = pc.x1;
        glyph.y1       = pc.y1;
        glyph.xOffset  = pc.xoff;
        glyph.yOffset  = pc.yoff;
        glyph.xAdvance = pc.xadvance;
        out.write(reinterpret_cast<const char*>(&glyph), sizeof(glyph));
    }

    out.write(reinterpret_cast<const char*>(atlas.data()),
              static_cast<std::streamsize>(atlas.size()));

    if (!out.good()) {
        std::fprintf(stderr, "aoc_font_bake: write failed for '%s'\n", outPath);
        return 1;
    }

    std::printf("aoc_font_bake: %s -> %s (%ux%u atlas, %u sizes %u-%u, %u glyphs)\n", usedPath,
                outPath, kAtlasWidth, kAtlasHeight, sizeCount, aoc::ui::FONT_ATLAS_MIN_SIZE,
                aoc::ui::FONT_ATLAS_MAX_SIZE, sizeCount * charCount);
    return 0;
}
