/**
 * @file TextureAtlas.cpp
 * @brief Sprite atlas manager implementation: grid-based UV lookup and placeholder generation.
 */

#include "aoc/render/TextureAtlas.hpp"

#include "vulkan_utils/Texture.hpp"
#include "vulkan_utils/Device.hpp"

#include <vulkan_utils/Logger.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace aoc::render {

// ============================================================================
// Construction / destruction
// ============================================================================

TextureAtlas::TextureAtlas(std::unique_ptr<vkutils::Texture> texture,
                           int32_t columnsPerRow, int32_t rowCount)
    : m_texture(std::move(texture))
    , m_columnsPerRow(columnsPerRow)
    , m_rowCount(rowCount) {
}

TextureAtlas::~TextureAtlas() = default;

TextureAtlas::TextureAtlas(TextureAtlas&& other) noexcept
    : m_texture(std::move(other.m_texture))
    , m_columnsPerRow(other.m_columnsPerRow)
    , m_rowCount(other.m_rowCount) {
    other.m_columnsPerRow = 0;
    other.m_rowCount = 0;
}

TextureAtlas& TextureAtlas::operator=(TextureAtlas&& other) noexcept {
    if (this != &other) {
        this->m_texture = std::move(other.m_texture);
        this->m_columnsPerRow = other.m_columnsPerRow;
        this->m_rowCount = other.m_rowCount;
        other.m_columnsPerRow = 0;
        other.m_rowCount = 0;
    }
    return *this;
}

// ============================================================================
// Factory methods
// ============================================================================

std::unique_ptr<TextureAtlas> TextureAtlas::loadFromFile(
    const vkutils::Device& device,
    const std::string& filepath,
    int32_t columnsPerRow,
    int32_t rowCount) {

    if (columnsPerRow <= 0 || rowCount <= 0) {
        VKLOG_ERROR("TextureAtlas::loadFromFile: invalid grid dimensions (%d x %d) [%s:%d]",
                    columnsPerRow, rowCount, __FILE__, __LINE__);
        return nullptr;
    }

    vkutils::Texture::LoadOptions options{};
    options.generateMipmaps = false;
    options.sRGB = true;
    options.filter = VK_FILTER_NEAREST;
    options.addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    options.anisotropy = 1.0f;
    options.flipVertically = false;

    std::unique_ptr<vkutils::Texture> texture =
        vkutils::Texture::loadFromFile(device, filepath, options);
    if (!texture) {
        VKLOG_ERROR("TextureAtlas::loadFromFile: failed to load '%s' [%s:%d]",
                    filepath.c_str(), __FILE__, __LINE__);
        return nullptr;
    }

    VKLOG_INFO("TextureAtlas: loaded '%s' (%ux%u, %dx%d grid = %d sprites)",
               filepath.c_str(), texture->getWidth(), texture->getHeight(),
               columnsPerRow, rowCount, columnsPerRow * rowCount);

    return std::unique_ptr<TextureAtlas>(
        new TextureAtlas(std::move(texture), columnsPerRow, rowCount));
}

std::unique_ptr<TextureAtlas> TextureAtlas::createPlaceholder(
    const vkutils::Device& device,
    int32_t columnsPerRow,
    int32_t rowCount) {

    if (columnsPerRow <= 0 || rowCount <= 0) {
        VKLOG_ERROR("TextureAtlas::createPlaceholder: invalid grid (%d x %d) [%s:%d]",
                    columnsPerRow, rowCount, __FILE__, __LINE__);
        return nullptr;
    }

    constexpr uint32_t ATLAS_SIZE = 256;
    constexpr uint32_t CHANNELS = 4;
    std::vector<unsigned char> pixels(ATLAS_SIZE * ATLAS_SIZE * CHANNELS);

    const uint32_t cellWidth = ATLAS_SIZE / static_cast<uint32_t>(columnsPerRow);
    const uint32_t cellHeight = ATLAS_SIZE / static_cast<uint32_t>(rowCount);

    // Deterministic palette: distinct hues for each cell
    for (uint32_t py = 0; py < ATLAS_SIZE; ++py) {
        for (uint32_t px = 0; px < ATLAS_SIZE; ++px) {
            const uint32_t col = px / cellWidth;
            const uint32_t row = py / cellHeight;
            const uint32_t cellId = row * static_cast<uint32_t>(columnsPerRow) + col;

            // Simple hash to spread colors across cells
            const uint32_t hash = (cellId * 2654435761u) >> 16;
            const uint8_t r = static_cast<uint8_t>(60 + (hash & 0xFFu) % 180);
            const uint8_t g = static_cast<uint8_t>(60 + ((hash >> 4) & 0xFFu) % 180);
            const uint8_t b = static_cast<uint8_t>(60 + ((hash >> 8) & 0xFFu) % 180);

            const uint32_t idx = (py * ATLAS_SIZE + px) * CHANNELS;
            pixels[idx + 0] = r;
            pixels[idx + 1] = g;
            pixels[idx + 2] = b;
            pixels[idx + 3] = 255;
        }
    }

    vkutils::Texture::LoadOptions options{};
    options.generateMipmaps = false;
    options.sRGB = true;
    options.filter = VK_FILTER_NEAREST;
    options.addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    options.anisotropy = 1.0f;
    options.flipVertically = false;

    std::unique_ptr<vkutils::Texture> texture =
        vkutils::Texture::loadFromMemory(device, pixels.data(),
                                          ATLAS_SIZE, ATLAS_SIZE, CHANNELS, options);
    if (!texture) {
        VKLOG_ERROR("TextureAtlas::createPlaceholder: GPU upload failed [%s:%d]",
                    __FILE__, __LINE__);
        return nullptr;
    }

    VKLOG_INFO("TextureAtlas: placeholder created (256x256, %dx%d grid = %d sprites)",
               columnsPerRow, rowCount, columnsPerRow * rowCount);

    return std::unique_ptr<TextureAtlas>(
        new TextureAtlas(std::move(texture), columnsPerRow, rowCount));
}

// ============================================================================
// Sprite lookup
// ============================================================================

SpriteRegion TextureAtlas::getSprite(int32_t id) const {
    const int32_t total = this->m_columnsPerRow * this->m_rowCount;
    const int32_t clampedId = std::clamp(id, 0, total - 1);

    const int32_t col = clampedId % this->m_columnsPerRow;
    const int32_t row = clampedId / this->m_columnsPerRow;

    const float cellU = 1.0f / static_cast<float>(this->m_columnsPerRow);
    const float cellV = 1.0f / static_cast<float>(this->m_rowCount);

    SpriteRegion region;
    region.u0 = static_cast<float>(col) * cellU;
    region.v0 = static_cast<float>(row) * cellV;
    region.u1 = region.u0 + cellU;
    region.v1 = region.v0 + cellV;
    return region;
}

int32_t TextureAtlas::spriteCount() const {
    return this->m_columnsPerRow * this->m_rowCount;
}

const vkutils::Texture& TextureAtlas::texture() const {
    return *this->m_texture;
}

} // namespace aoc::render
