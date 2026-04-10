#pragma once

/**
 * @file SpriteRenderer.hpp
 * @brief Batched sprite renderer using a texture atlas and instanced draw calls.
 *
 * Separate from Renderer2D: this class owns its own Vulkan pipeline that
 * samples a bound atlas texture and supports per-sprite tint colors.
 * All sprites are drawn in a single instanced draw call per flush.
 */

#include "aoc/render/TextureAtlas.hpp"
#include "vulkan_utils/Buffer.hpp"

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <cstdint>

namespace vkutils {
class Device;
class PipelineLayout;
using PipelineLayoutPtr = std::unique_ptr<PipelineLayout>;
}

namespace aoc::render {

/**
 * @brief Per-instance data for a single sprite quad.
 *
 * Stores world-space position/size, UV atlas region, and tint color.
 * Packed to 48 bytes (12 floats) for efficient GPU upload.
 */
struct alignas(16) SpriteInstanceData {
    float posX = 0.0f;   ///< Top-left X (world space)
    float posY = 0.0f;   ///< Top-left Y (world space)
    float sizeX = 0.0f;  ///< Width in world units
    float sizeY = 0.0f;  ///< Height in world units
    float u0 = 0.0f;     ///< Atlas UV left
    float v0 = 0.0f;     ///< Atlas UV top
    float u1 = 1.0f;     ///< Atlas UV right
    float v1 = 1.0f;     ///< Atlas UV bottom
    float r = 1.0f;      ///< Tint red
    float g = 1.0f;      ///< Tint green
    float b = 1.0f;      ///< Tint blue
    float a = 1.0f;      ///< Tint alpha
};

/**
 * @brief GPU-accelerated batched sprite renderer.
 *
 * Binds one atlas texture and draws all queued sprites in a single
 * instanced draw call. Uses the same camera transform convention as
 * Renderer2D (push constant with screen size, camera position, zoom).
 *
 * Usage:
 * @code
 *   spriteRenderer.beginFrame(frameIndex);
 *   spriteRenderer.begin();
 *   spriteRenderer.drawSprite(100, 200, 64, 64, region, 1, 1, 1, 1);
 *   spriteRenderer.end(commandBuffer);
 * @endcode
 */
class SpriteRenderer {
public:
    /// Camera/projection push constant data (matches Renderer2D layout)
    struct CameraData {
        float screenWidth;
        float screenHeight;
        float cameraX;
        float cameraY;
        float zoom;
        float _padding[3];
    };

    static constexpr uint32_t MAX_SPRITES = 16384;
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    /**
     * @param device Vulkan device
     * @param renderPass Compatible render pass
     * @param extent Swapchain extent (viewport size)
     * @param atlas Sprite atlas to bind (must outlive this renderer)
     * @param framesInFlight Number of frames-in-flight for buffer rotation
     */
    SpriteRenderer(const vkutils::Device& device,
                   VkRenderPass renderPass,
                   VkExtent2D extent,
                   const TextureAtlas& atlas,
                   uint32_t framesInFlight = MAX_FRAMES_IN_FLIGHT);
    ~SpriteRenderer();

    SpriteRenderer(const SpriteRenderer&) = delete;
    SpriteRenderer& operator=(const SpriteRenderer&) = delete;

    /** @brief Select the per-frame instance buffer. Call once per frame. */
    void beginFrame(uint32_t frameIndex);

    /** @brief Clear the sprite batch for a new draw pass. */
    void begin();

    /** @brief Flush all queued sprites to the command buffer. */
    void end(VkCommandBuffer cmdBuffer);

    /**
     * @brief Queue a textured sprite for drawing.
     * @param x Top-left X position (world space)
     * @param y Top-left Y position (world space)
     * @param w Width in world units
     * @param h Height in world units
     * @param region UV region within the atlas
     * @param r Tint red [0,1]
     * @param g Tint green [0,1]
     * @param b Tint blue [0,1]
     * @param a Tint alpha [0,1]
     */
    void drawSprite(float x, float y, float w, float h,
                    SpriteRegion region,
                    float r, float g, float b, float a = 1.0f);

    void setCamera(float x, float y);
    void setZoom(float zoom);
    void setExtent(VkExtent2D extent);
    void resetCamera();

    [[nodiscard]] uint32_t getSpriteCount() const;

private:
    void createDescriptorResources(const TextureAtlas& atlas);
    void createPipeline(VkRenderPass renderPass);
    void createBuffers();
    void flushBatch(VkCommandBuffer cmdBuffer);
    void cleanup();

    struct Vertex2D {
        float x, y;
        float u, v;
    };

    const vkutils::Device& m_device;
    VkExtent2D m_extent;

    // Pipeline
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    vkutils::PipelineLayoutPtr m_pipelineLayout;

    // Descriptor set for the atlas texture
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;

    // Vertex buffer (unit quad)
    vkutils::BufferPtr m_vertexBuffer;

    // Per-frame instance buffers
    std::vector<vkutils::BufferPtr> m_instanceBuffers;
    uint32_t m_currentFrameIndex = 0;

    // Batched sprites for current frame
    std::vector<SpriteInstanceData> m_sprites;

    // Camera
    CameraData m_camera{};
};

} // namespace aoc::render
