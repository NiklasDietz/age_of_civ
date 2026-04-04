#pragma once

/**
 * @file GameRenderer.hpp
 * @brief Top-level render orchestrator. Drives MapRenderer, UnitRenderer, UIRenderer.
 */

#include "aoc/render/MapRenderer.hpp"
#include "aoc/render/UnitRenderer.hpp"
#include "aoc/ui/UIManager.hpp"

#include <cstdint>
#include <vulkan/vulkan.h>

namespace vulkan_app {
class RenderPipeline;
namespace renderer {
class Renderer2D;
}
}

namespace aoc::ecs {
class World;
}

namespace aoc::map {
class HexGrid;
}

namespace aoc::render {

class CameraController;

class GameRenderer {
public:
    GameRenderer() = default;

    void initialize(vulkan_app::RenderPipeline& pipeline,
                    vulkan_app::renderer::Renderer2D& renderer2d);

    void render(vulkan_app::renderer::Renderer2D& renderer2d,
                VkCommandBuffer commandBuffer,
                uint32_t frameIndex,
                const CameraController& camera,
                const aoc::map::HexGrid& grid,
                const aoc::ecs::World& world,
                aoc::ui::UIManager& uiManager,
                uint32_t screenWidth, uint32_t screenHeight);

    MapRenderer&  mapRenderer()  { return this->m_mapRenderer; }
    UnitRenderer& unitRenderer() { return this->m_unitRenderer; }

private:
    MapRenderer  m_mapRenderer;
    UnitRenderer m_unitRenderer;
};

} // namespace aoc::render
