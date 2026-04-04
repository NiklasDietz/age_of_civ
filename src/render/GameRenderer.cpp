/**
 * @file GameRenderer.cpp
 * @brief Top-level render orchestrator implementation.
 */

#include "aoc/render/GameRenderer.hpp"
#include "aoc/render/CameraController.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/ecs/World.hpp"

#include <renderer/Renderer2D.hpp>
#include <renderer/RenderPipeline.hpp>

namespace aoc::render {

void GameRenderer::initialize(vulkan_app::RenderPipeline& /*pipeline*/,
                               vulkan_app::renderer::Renderer2D& /*renderer2d*/) {
}

void GameRenderer::render(vulkan_app::renderer::Renderer2D& renderer2d,
                           VkCommandBuffer commandBuffer,
                           uint32_t frameIndex,
                           const CameraController& camera,
                           const aoc::map::HexGrid& grid,
                           const aoc::ecs::World& world,
                           aoc::ui::UIManager& uiManager,
                           uint32_t screenWidth, uint32_t screenHeight) {
    float hexSize = this->m_mapRenderer.hexSize();

    // -- World-space rendering (with camera) --
    renderer2d.setCamera(camera.cameraX(), camera.cameraY());
    renderer2d.setZoom(camera.zoom());

    renderer2d.beginFrame(frameIndex);
    renderer2d.begin();

    this->m_mapRenderer.draw(renderer2d, grid, camera, screenWidth, screenHeight);
    this->m_unitRenderer.drawCities(renderer2d, world, camera, hexSize,
                                     screenWidth, screenHeight);
    this->m_unitRenderer.drawUnits(renderer2d, world, camera, hexSize,
                                    screenWidth, screenHeight);

    renderer2d.end(commandBuffer);

    // -- Screen-space rendering (UI overlay, no camera) --
    renderer2d.resetCamera();
    renderer2d.setZoom(1.0f);

    renderer2d.begin();

    uiManager.layout();
    uiManager.render(renderer2d);

    renderer2d.end(commandBuffer);
}

} // namespace aoc::render
