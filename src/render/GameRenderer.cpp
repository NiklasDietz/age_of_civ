/**
 * @file GameRenderer.cpp
 * @brief Top-level render orchestrator implementation.
 *
 * The Renderer2D uses a single GPU instance buffer per frame. Multiple
 * begin/end batches overwrite each other. We render everything in one batch
 * using the world-space camera, and transform UI positions to world space
 * so the camera shader maps them back to the correct screen positions.
 */

#include "aoc/render/GameRenderer.hpp"
#include "aoc/render/CameraController.hpp"
#include "aoc/ui/Notifications.hpp"
#include "aoc/ui/Tutorial.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/FogOfWar.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/ui/BitmapFont.hpp"

#include <renderer/Renderer2D.hpp>
#include <renderer/RenderPipeline.hpp>

namespace {

/// Player colors for city name labels (matches UnitRenderer).
constexpr std::array<std::array<float, 3>, 8> LABEL_PLAYER_COLORS = {{
    {0.20f, 0.40f, 0.90f},
    {0.90f, 0.20f, 0.20f},
    {0.20f, 0.80f, 0.20f},
    {0.90f, 0.80f, 0.10f},
    {0.70f, 0.30f, 0.80f},
    {0.90f, 0.50f, 0.10f},
    {0.10f, 0.80f, 0.80f},
    {0.80f, 0.40f, 0.60f},
}};

} // anonymous namespace

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
                           const aoc::map::FogOfWar& fog,
                           PlayerId viewingPlayer,
                           aoc::ui::UIManager& uiManager,
                           uint32_t screenWidth, uint32_t screenHeight,
                           const aoc::ui::EventLog* eventLog,
                           const aoc::ui::NotificationManager* notifications,
                           const aoc::ui::TutorialManager* tutorial) {
    float hexSize = this->m_mapRenderer.hexSize();

    // The shader's cameraPos is the TOP-LEFT corner of the viewport in world space.
    // Our CameraController stores the CENTER of the viewport.
    float topLeftX = camera.cameraX() - static_cast<float>(screenWidth)  / (2.0f * camera.zoom());
    float topLeftY = camera.cameraY() - static_cast<float>(screenHeight) / (2.0f * camera.zoom());

    renderer2d.setCamera(topLeftX, topLeftY);
    renderer2d.setZoom(camera.zoom());

    renderer2d.beginFrame(frameIndex);
    renderer2d.begin();

    // Layer 1: Map tiles (world coordinates -- shader transforms via camera)
    this->m_mapRenderer.draw(renderer2d, grid, fog, viewingPlayer, camera,
                              screenWidth, screenHeight);

    // Layer 1.5: Territory borders (hex outlines drawn ON TOP of terrain)
    this->m_mapRenderer.drawTerritoryBorders(renderer2d, grid, camera,
                                              screenWidth, screenHeight);

    // Layer 1.55: Tile yield labels (if enabled in settings)
    if (this->showTileYields) {
        this->m_mapRenderer.drawYieldLabels(renderer2d, grid, fog, viewingPlayer,
                                             camera, screenWidth, screenHeight);
    }

    // Layer 1.6: City tile highlight overlay (when a city is selected, dim non-interactable tiles)
    if (this->m_unitRenderer.selectedEntity.isValid() &&
        world.hasComponent<aoc::sim::CityComponent>(this->m_unitRenderer.selectedEntity)) {
        const aoc::sim::CityComponent& selCity =
            world.getComponent<aoc::sim::CityComponent>(this->m_unitRenderer.selectedEntity);

        // Compute visible bounds
        float tlx = 0.0f, tly = 0.0f, brx = 0.0f, bry = 0.0f;
        camera.screenToWorld(0.0, 0.0, tlx, tly, screenWidth, screenHeight);
        camera.screenToWorld(static_cast<double>(screenWidth), static_cast<double>(screenHeight),
                             brx, bry, screenWidth, screenHeight);
        float margin = hexSize * 2.0f;
        tlx -= margin; tly -= margin; brx += margin; bry += margin;

        constexpr float SQRT3 = 1.7320508075688772f;
        float xSpacing = SQRT3 * hexSize;
        float ySpacing = 1.5f * hexSize;
        int32_t minCol = std::max(0, static_cast<int32_t>(tlx / xSpacing) - 1);
        int32_t maxCol = std::min(grid.width() - 1, static_cast<int32_t>(brx / xSpacing) + 1);
        int32_t minRow = std::max(0, static_cast<int32_t>(tly / ySpacing) - 1);
        int32_t maxRow = std::min(grid.height() - 1, static_cast<int32_t>(bry / ySpacing) + 1);

        for (int32_t row = minRow; row <= maxRow; ++row) {
            for (int32_t col = minCol; col <= maxCol; ++col) {
                int32_t index = row * grid.width() + col;
                aoc::hex::AxialCoord axial = aoc::hex::offsetToAxial({col, row});
                float cx2 = 0.0f, cy2 = 0.0f;
                aoc::hex::axialToPixel(axial, hexSize, cx2, cy2);
                if (cx2 < tlx || cx2 > brx || cy2 < tly || cy2 > bry) { continue; }

                aoc::map::TileVisibility vis = fog.visibility(viewingPlayer, index);
                if (vis == aoc::map::TileVisibility::Unseen) { continue; }

                int32_t dist = aoc::hex::distance(selCity.location, axial);
                bool isWorked = false;
                for (const aoc::hex::AxialCoord& wt : selCity.workedTiles) {
                    if (wt == axial) { isWorked = true; break; }
                }
                bool isBuyable = (grid.owner(index) == INVALID_PLAYER);
                if (isBuyable) {
                    // Check adjacency to owned tile
                    bool adj = false;
                    std::array<aoc::hex::AxialCoord, 6> nbrs = aoc::hex::neighbors(axial);
                    for (const aoc::hex::AxialCoord& n : nbrs) {
                        if (grid.isValid(n) && grid.owner(grid.toIndex(n)) == viewingPlayer) {
                            adj = true; break;
                        }
                    }
                    isBuyable = adj;
                }

                float hexW = hexSize * 0.866f;
                float hexH = hexSize;

                if (isWorked) {
                    // Green highlight for worked tiles
                    renderer2d.drawFilledHexagon(cx2, cy2, hexW, hexH,
                                                 0.1f, 0.5f, 0.1f, 0.20f);
                    // White outline on worked tiles
                    renderer2d.drawHexagonOutline(cx2, cy2, hexW * 0.95f, hexH * 0.95f,
                                                  0.3f, 0.8f, 0.3f, 0.5f, 1.5f);
                } else if (isBuyable) {
                    // Get player treasury to determine if affordable
                    CurrencyAmount playerTreasury = 0;
                    const aoc::ecs::ComponentPool<aoc::sim::PlayerEconomyComponent>* econPool =
                        world.getPool<aoc::sim::PlayerEconomyComponent>();
                    if (econPool != nullptr) {
                        for (uint32_t ei = 0; ei < econPool->size(); ++ei) {
                            if (econPool->data()[ei].owner == viewingPlayer) {
                                playerTreasury = econPool->data()[ei].treasury;
                                break;
                            }
                        }
                    }
                    int32_t tileCost = 25 * std::max(1, dist);
                    bool canAfford = (playerTreasury >= static_cast<CurrencyAmount>(tileCost));

                    if (canAfford) {
                        // Green: affordable
                        renderer2d.drawFilledHexagon(cx2, cy2, hexW, hexH,
                                                     0.1f, 0.4f, 0.1f, 0.25f);
                        renderer2d.drawHexagonOutline(cx2, cy2, hexW * 0.95f, hexH * 0.95f,
                                                      0.2f, 0.7f, 0.2f, 0.6f, 1.5f);
                    } else {
                        // Red: too expensive
                        renderer2d.drawFilledHexagon(cx2, cy2, hexW, hexH,
                                                     0.4f, 0.1f, 0.1f, 0.25f);
                        renderer2d.drawHexagonOutline(cx2, cy2, hexW * 0.95f, hexH * 0.95f,
                                                      0.7f, 0.2f, 0.2f, 0.6f, 1.5f);
                    }
                    // Show price text on the tile
                    float invZoomTile = 1.0f / camera.zoom();
                    char priceBuf[16];
                    std::snprintf(priceBuf, sizeof(priceBuf), "%dg", tileCost);
                    aoc::ui::Color priceColor = canAfford
                        ? aoc::ui::Color{0.3f, 0.9f, 0.3f, 0.9f}
                        : aoc::ui::Color{0.9f, 0.3f, 0.3f, 0.9f};
                    aoc::ui::BitmapFont::drawText(renderer2d, priceBuf,
                        cx2 - hexSize * 0.2f, cy2 - hexSize * 0.15f,
                        9.0f * invZoomTile, priceColor, invZoomTile);
                } else if (dist > 4) {
                    // Darken distant tiles
                    renderer2d.drawFilledHexagon(cx2, cy2, hexW, hexH,
                                                 0.0f, 0.0f, 0.0f, 0.4f);
                }

                // Worker indicator circle on workable tiles (owned, within range)
                bool isOwned = (grid.owner(index) == viewingPlayer);
                if (isOwned && dist <= 3 && axial != selCity.location) {
                    float circleR = hexSize * 0.18f;
                    float circleX = cx2;
                    float circleY = cy2 - hexSize * 0.25f;
                    bool isLocked = selCity.isTileLocked(axial);

                    if (isWorked) {
                        // Filled green circle with head icon
                        renderer2d.drawFilledCircle(circleX, circleY, circleR,
                                                     0.15f, 0.55f, 0.15f, 0.9f);
                        renderer2d.drawCircle(circleX, circleY, circleR,
                                               0.3f, 0.8f, 0.3f, 0.9f, 1.5f);

                        // Head icon: small circle (head) + triangle (body)
                        float headR = circleR * 0.3f;
                        renderer2d.drawFilledCircle(circleX, circleY - headR * 0.6f, headR,
                                                     1.0f, 1.0f, 1.0f, 0.9f);
                        // Shoulders/body arc
                        renderer2d.drawFilledArc(circleX, circleY + headR * 0.8f, headR * 1.5f,
                                                  3.14159f, 0.0f,
                                                  1.0f, 1.0f, 1.0f, 0.8f);
                    } else {
                        // Empty circle (available slot)
                        renderer2d.drawCircle(circleX, circleY, circleR,
                                               0.5f, 0.5f, 0.5f, 0.5f, 1.0f);
                    }

                    // Lock icon overlay (small padlock shape)
                    if (isLocked) {
                        float lockX = circleX + circleR * 0.7f;
                        float lockY = circleY - circleR * 0.7f;
                        float lockS = circleR * 0.45f;
                        // Lock body (filled rect)
                        renderer2d.drawFilledRect(lockX - lockS * 0.5f, lockY,
                                                   lockS, lockS * 0.8f,
                                                   0.9f, 0.75f, 0.2f, 0.9f);
                        // Lock shackle (arc/circle at top)
                        renderer2d.drawCircle(lockX, lockY, lockS * 0.4f,
                                               0.9f, 0.75f, 0.2f, 0.9f, 1.5f);
                    }
                }
            }
        }
    }

    // Layer 2: Cities (world coordinates)
    this->m_unitRenderer.drawCities(renderer2d, world, fog, grid, viewingPlayer,
                                     camera, hexSize, screenWidth, screenHeight);

    // Layer 3: Units (world coordinates)
    this->m_unitRenderer.drawUnits(renderer2d, world, fog, grid, viewingPlayer,
                                    camera, hexSize, screenWidth, screenHeight);

    // Layer 3.5: City name labels (world-space text above each city hex)
    {
        const aoc::ecs::ComponentPool<aoc::sim::CityComponent>* cityPool =
            world.getPool<aoc::sim::CityComponent>();
        if (cityPool != nullptr) {
            const float invZoomLabel = 1.0f / camera.zoom();
            constexpr float LABEL_FONT_SIZE = 10.0f;
            const float labelOffsetY = hexSize * 0.60f;

            for (uint32_t ci = 0; ci < cityPool->size(); ++ci) {
                const aoc::sim::CityComponent& city = cityPool->data()[ci];

                // Skip cities on unseen tiles
                if (grid.isValid(city.location)) {
                    int32_t tileIdx = grid.toIndex(city.location);
                    aoc::map::TileVisibility vis = fog.visibility(viewingPlayer, tileIdx);
                    if (vis == aoc::map::TileVisibility::Unseen) {
                        continue;
                    }
                }

                float cityCx = 0.0f, cityCy = 0.0f;
                hex::axialToPixel(city.location, hexSize, cityCx, cityCy);

                // Measure text at screen size, then convert to world width.
                // drawText with pixelScale rasterizes at fontSize/pixelScale screen pixels,
                // and each pixel occupies pixelScale world units.
                // So total world width = measureText(fontSize).w * pixelScale
                // (measureText returns screen-pixel width at the given fontSize)
                const aoc::ui::Rect textBounds =
                    aoc::ui::BitmapFont::measureText(city.name, LABEL_FONT_SIZE);
                const float textWorldW = textBounds.w * invZoomLabel;
                const float textWorldH = textBounds.h * invZoomLabel;
                const float textX = cityCx - textWorldW * 0.5f;
                const float textY = cityCy - labelOffsetY - textWorldH * 0.5f;

                // Player color
                const std::size_t cIdx =
                    static_cast<std::size_t>(city.owner) % LABEL_PLAYER_COLORS.size();
                const aoc::ui::Color labelColor{
                    LABEL_PLAYER_COLORS[cIdx][0],
                    LABEL_PLAYER_COLORS[cIdx][1],
                    LABEL_PLAYER_COLORS[cIdx][2],
                    1.0f};

                aoc::ui::BitmapFont::drawText(renderer2d, city.name,
                                               textX, textY,
                                               LABEL_FONT_SIZE, labelColor,
                                               invZoomLabel);
            }
        }
    }

    // Layer 3.5b: Combat animations (between units layer and UI)
    this->m_combatAnimator.render(renderer2d, hexSize);

    // Layer 3.5c: Particle effects
    this->m_particleSystem.render(renderer2d);

    // Layer 3.6: Ranged attack range overlay for selected ranged unit
    if (this->m_unitRenderer.selectedEntity.isValid() &&
        world.hasComponent<aoc::sim::UnitComponent>(this->m_unitRenderer.selectedEntity)) {
        const aoc::sim::UnitComponent& selUnit =
            world.getComponent<aoc::sim::UnitComponent>(this->m_unitRenderer.selectedEntity);
        const aoc::sim::UnitTypeDef& selDef = aoc::sim::unitTypeDef(selUnit.typeId);
        if (selDef.rangedStrength > 0 && selDef.range > 0) {
            this->m_unitRenderer.drawRangedRange(renderer2d, world,
                                                  selUnit.position, selDef.range,
                                                  selUnit.owner, hexSize);
        }
    }

    // Layer 4: UI overlay (single batch - transform UI to world-space so the
    // camera shader maps it back to screen-space correctly).
    float invZoom = 1.0f / camera.zoom();
    uiManager.layout();
    uiManager.transformBounds(topLeftX, topLeftY, invZoom);
    uiManager.render(renderer2d);
    uiManager.untransformBounds(topLeftX, topLeftY, invZoom);

    // Tooltip (transform screen-space mouse position to world-space for rendering)
    if (this->m_tooltipManager.isVisible()) {
        float savedX = this->m_tooltipManager.getX();
        float savedY = this->m_tooltipManager.getY();
        this->m_tooltipManager.setPosition(
            topLeftX + savedX * invZoom,
            topLeftY + savedY * invZoom);
        this->m_tooltipManager.setRenderScale(invZoom);
        this->m_tooltipManager.render(renderer2d);
        this->m_tooltipManager.setPosition(savedX, savedY);
    }

    // Event log (transformed to world-space)
    if (eventLog != nullptr && !eventLog->events().empty()) {
        constexpr float EVENT_LOG_W = 320.0f;
        constexpr float EVENT_LOG_H = 160.0f;
        constexpr float EVENT_LOG_MARGIN = 10.0f;
        float elScreenX = static_cast<float>(screenWidth) - EVENT_LOG_W - EVENT_LOG_MARGIN;
        float elScreenY = static_cast<float>(screenHeight) - EVENT_LOG_H - 70.0f;
        float elWorldX = topLeftX + elScreenX * invZoom;
        float elWorldY = topLeftY + elScreenY * invZoom;
        eventLog->render(renderer2d, elWorldX, elWorldY,
                          EVENT_LOG_W * invZoom, EVENT_LOG_H * invZoom, invZoom);
    }

    // Minimap (transformed to world-space)
    constexpr float MINIMAP_W = 200.0f;
    constexpr float MINIMAP_H = 130.0f;
    constexpr float MINIMAP_MARGIN = 10.0f;
    float mmScreenX = MINIMAP_MARGIN;
    float mmScreenY = static_cast<float>(screenHeight) - MINIMAP_H - MINIMAP_MARGIN;
    float mmWorldX = topLeftX + mmScreenX * invZoom;
    float mmWorldY = topLeftY + mmScreenY * invZoom;
    this->m_minimap.draw(renderer2d, grid, fog, viewingPlayer, camera,
                         mmWorldX, mmWorldY, MINIMAP_W * invZoom, MINIMAP_H * invZoom,
                         screenWidth, screenHeight);

    // Notifications (transformed to world-space)
    if (notifications != nullptr) {
        notifications->render(renderer2d,
                               static_cast<float>(screenWidth),
                               static_cast<float>(screenHeight),
                               invZoom);
    }

    // Tutorial overlay (transformed to world-space)
    if (tutorial != nullptr && tutorial->isActive()) {
        tutorial->render(renderer2d,
                          static_cast<float>(screenWidth),
                          static_cast<float>(screenHeight),
                          invZoom);
    }

    renderer2d.end(commandBuffer);
}

} // namespace aoc::render
