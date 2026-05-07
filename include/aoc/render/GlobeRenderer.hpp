#pragma once

/**
 * @file GlobeRenderer.hpp
 * @brief 3D textured-sphere render mode for the Continent Creator.
 *
 * Wraps a Renderer3D instance plus an internal sphere mesh and
 * per-terrain material palette. Each scrub step the caller invokes
 * `updateFromGrid()` to rebuild per-terrain sub-meshes from the
 * current HexGrid; `render()` then submits one draw call per
 * terrain type (10-11 total) via the forward3d pipeline.
 *
 * The orbit camera is parametrised by yaw/pitch/zoom -- yaw rotates
 * around the world Y axis (longitude), pitch around the world X axis
 * (latitude), zoom = camera radius in unit-sphere multiples.
 */

#include "aoc/map/HexGrid.hpp"

#include <vulkan/vulkan.h>

#include <memory>
#include <cstdint>

namespace vkutils { class Device; }

namespace vulkan_app {
class Renderer3D;
}

namespace aoc::render {

class GlobeRenderer {
public:
    GlobeRenderer();
    ~GlobeRenderer();

    GlobeRenderer(const GlobeRenderer&)            = delete;
    GlobeRenderer& operator=(const GlobeRenderer&) = delete;

    /// One-shot init. Owns its own Renderer3D bound to the supplied
    /// render pass + device. Must be called before any render().
    void initialize(const vkutils::Device& device,
                    VkRenderPass renderPass,
                    VkExtent2D extent);

    /// Resize the underlying Renderer3D viewport. Call after window
    /// resize.
    void setExtent(VkExtent2D extent);

    /// Mark internal mesh cache stale. Call when the HexGrid contents
    /// change (re-roll, scrub-to-new-epoch). Next render() rebuilds
    /// the per-terrain sub-meshes.
    void markGridDirty();

    /// Internal: rebuild per-terrain sub-meshes from the supplied
    /// HexGrid. Issues a device-wait before destroying old buffers so
    /// in-flight GPU work doesn't reference freed memory. Only called
    /// from render() when markGridDirty() was set; never call directly.
    void updateFromGrid(const aoc::map::HexGrid& grid);

    /// Submit draw calls for the globe view this frame. Builds the
    /// camera matrix from yaw/pitch/zoom (degrees / unit-sphere
    /// multiples), adds a sun directional light, submits one draw per
    /// terrain sub-mesh, calls Renderer3D::render() inline, and ends
    /// the frame. If the grid has been marked dirty (markGridDirty)
    /// since the previous render, this also rebuilds the sub-meshes
    /// from `grid` first. Must be called inside an active render pass.
    void render(VkCommandBuffer cmd, uint32_t frameIndex,
                const aoc::map::HexGrid& grid,
                float yawDeg, float pitchDeg, float zoom,
                float aspect);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace aoc::render
