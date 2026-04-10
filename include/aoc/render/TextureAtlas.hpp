#pragma once

/**
 * @file TextureAtlas.hpp
 * @brief Sprite atlas manager that divides a single texture into a grid of sprites.
 *
 * Loads a single atlas PNG and provides UV coordinates for individual sprites
 * addressed by integer ID. Sprites are arranged in a uniform grid within
 * the atlas texture.
 */

#include <cstdint>
#include <memory>
#include <string>

namespace vkutils {
class Device;
class Texture;
}

namespace aoc::render {

/**
 * @brief UV rectangle within an atlas texture, identifying one sprite.
 *
 * Coordinates are normalized [0,1] relative to the full atlas texture.
 */
struct SpriteRegion {
    float u0 = 0.0f;  ///< Left edge U
    float v0 = 0.0f;  ///< Top edge V
    float u1 = 1.0f;  ///< Right edge U
    float v1 = 1.0f;  ///< Bottom edge V
};

/**
 * @brief Manages a sprite atlas: a single texture subdivided into a grid of sprites.
 *
 * Each sprite is addressed by a sequential ID (row-major order). The atlas
 * can be loaded from a PNG file or created as a placeholder at runtime.
 */
class TextureAtlas {
public:
    /**
     * @brief Load an atlas from a PNG file with a known grid layout.
     * @param device Vulkan device for GPU resource creation
     * @param filepath Path to the atlas PNG image
     * @param columnsPerRow Number of sprite columns in the atlas
     * @param rowCount Number of sprite rows in the atlas
     * @return Loaded atlas, or nullptr on failure
     */
    [[nodiscard]] static std::unique_ptr<TextureAtlas> loadFromFile(
        const vkutils::Device& device,
        const std::string& filepath,
        int32_t columnsPerRow,
        int32_t rowCount);

    /**
     * @brief Create a placeholder atlas with colored squares for each sprite cell.
     *
     * Generates a 256x256 RGBA texture divided into the given grid, with each
     * cell filled by a distinct solid color. Useful for development before
     * real art assets are available.
     *
     * @param device Vulkan device for GPU resource creation
     * @param columnsPerRow Number of sprite columns
     * @param rowCount Number of sprite rows
     * @return Placeholder atlas, or nullptr on failure
     */
    [[nodiscard]] static std::unique_ptr<TextureAtlas> createPlaceholder(
        const vkutils::Device& device,
        int32_t columnsPerRow,
        int32_t rowCount);

    ~TextureAtlas();

    TextureAtlas(const TextureAtlas&) = delete;
    TextureAtlas& operator=(const TextureAtlas&) = delete;
    TextureAtlas(TextureAtlas&& other) noexcept;
    TextureAtlas& operator=(TextureAtlas&& other) noexcept;

    /**
     * @brief Get the UV region for a sprite by its sequential ID (row-major).
     * @param id Sprite index (0-based). Clamped to valid range.
     * @return UV rectangle for the requested sprite
     */
    [[nodiscard]] SpriteRegion getSprite(int32_t id) const;

    /** @brief Total number of sprite cells in the atlas grid. */
    [[nodiscard]] int32_t spriteCount() const;

    /** @brief The underlying GPU texture. */
    [[nodiscard]] const vkutils::Texture& texture() const;

private:
    TextureAtlas(std::unique_ptr<vkutils::Texture> texture,
                 int32_t columnsPerRow, int32_t rowCount);

    std::unique_ptr<vkutils::Texture> m_texture;
    int32_t m_columnsPerRow;
    int32_t m_rowCount;
};

} // namespace aoc::render
