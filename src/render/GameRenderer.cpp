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
#include "aoc/render/MapOverlays.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/game/City.hpp"
#include "aoc/ui/Notifications.hpp"
#include "aoc/ui/Tutorial.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/FogOfWar.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
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
                           const aoc::game::GameState& gameState,
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

    // Layer 1.1: Map view overlay (e.g. tectonic plates). Draws a
    // semi-transparent tint per tile keyed off the selected mode. Each
    // overlay maps a per-tile category (plate id, government id, etc.)
    // to a deterministic colour so neighbouring tiles in the same
    // category share a hue. None mode skips the pass entirely.
    if (this->overlayMode != MapOverlay::None) {
        const float hexSizeOv = this->m_mapRenderer.hexSize();
        const float invZoomOv = 1.0f / camera.zoom();
        float ovTL_X = 0.0f, ovTL_Y = 0.0f;
        float ovBR_X = 0.0f, ovBR_Y = 0.0f;
        camera.screenToWorld(0.0, 0.0, ovTL_X, ovTL_Y,
                              screenWidth, screenHeight);
        camera.screenToWorld(static_cast<double>(screenWidth),
                              static_cast<double>(screenHeight),
                              ovBR_X, ovBR_Y,
                              screenWidth, screenHeight);
        const float marginOv = hexSizeOv * 6.0f * invZoomOv;
        ovTL_X -= marginOv;  ovTL_Y -= marginOv;
        ovBR_X += marginOv;  ovBR_Y += marginOv;

        constexpr float SQRT3 = 1.7320508075688772f;
        const float xSpacing = SQRT3 * hexSizeOv;
        const float ySpacing = 1.5f  * hexSizeOv;
        const int32_t width  = grid.width();
        const int32_t height = grid.height();
        const bool cyl = (grid.topology() == aoc::map::MapTopology::Cylindrical);
        const int32_t minCol = cyl
            ? (static_cast<int32_t>(ovTL_X / xSpacing) - 1)
            : std::max(0, static_cast<int32_t>(std::max(0.0f, ovTL_X) / xSpacing) - 1);
        const int32_t maxCol = cyl
            ? (static_cast<int32_t>(ovBR_X / xSpacing) + 1)
            : std::min(width - 1,
                static_cast<int32_t>(std::max(0.0f, ovBR_X) / xSpacing) + 1);
        const int32_t minRow = std::max(0,
            static_cast<int32_t>(std::max(0.0f, ovTL_Y) / ySpacing) - 1);
        const int32_t maxRow = std::min(height - 1,
            static_cast<int32_t>(std::max(0.0f, ovBR_Y) / ySpacing) + 1);

        // Helper: deterministic hue per category id.
        const auto categoryColor = [](uint8_t cat) {
            if (cat == 0xFF) {
                return std::array<float, 3>{0.0f, 0.0f, 0.0f};
            }
            const uint32_t h = static_cast<uint32_t>(cat) * 2654435761u;
            const float r = static_cast<float>((h >> 0)  & 0xFFu) / 255.0f;
            const float g = static_cast<float>((h >> 8)  & 0xFFu) / 255.0f;
            const float b = static_cast<float>((h >> 16) & 0xFFu) / 255.0f;
            // Pull saturation up + brightness so the overlay reads
            // clearly on top of darker terrain.
            return std::array<float, 3>{
                std::min(1.0f, r * 0.7f + 0.3f),
                std::min(1.0f, g * 0.7f + 0.3f),
                std::min(1.0f, b * 0.7f + 0.3f)};
        };

        for (int32_t row = minRow; row <= maxRow; ++row) {
            for (int32_t col = minCol; col <= maxCol; ++col) {
                int32_t dataCol = col;
                if (cyl) {
                    dataCol = ((col % width) + width) % width;
                } else {
                    if (col < 0 || col >= width) { continue; }
                }
                const int32_t index = row * width + dataCol;
                if (fog.visibility(viewingPlayer, index)
                        == aoc::map::TileVisibility::Unseen) {
                    continue;
                }

                uint8_t cat = 0xFF;
                if (this->overlayMode == MapOverlay::TectonicPlates) {
                    cat = grid.plateId(index);
                }
                if (cat == 0xFFu) { continue; }

                const aoc::hex::AxialCoord axial =
                    aoc::hex::offsetToAxial({col, row});
                float cx = 0.0f, cy = 0.0f;
                aoc::hex::axialToPixel(axial, hexSizeOv, cx, cy);
                const std::array<float, 3> rgb = categoryColor(cat);
                renderer2d.drawFilledHexagon(cx, cy,
                    hexSizeOv * 0.866f, hexSizeOv,
                    rgb[0], rgb[1], rgb[2], 0.45f);
            }
        }
    }

    // Layer 1.5: Territory borders (hex outlines drawn ON TOP of terrain)
    this->m_mapRenderer.drawTerritoryBorders(renderer2d, grid, fog, viewingPlayer,
                                              camera, screenWidth, screenHeight);

    // WP-J: adjacency arrows from hovered tile. World-space call — safe to
    // emit from within the main camera-transformed pass; drawn only when
    // tooltip manager has a valid hovered tile.
    if (this->m_tooltipManager.hasHovered()) {
        aoc::render::renderAdjacencyArrowOverlay(
            renderer2d, grid, this->m_tooltipManager.hoveredTile(),
            0.0f, 0.0f, 1.0f);
    }

    // Layer 1.55: Tile yield labels (if enabled in settings)
    if (this->showTileYields) {
        this->m_mapRenderer.drawYieldLabels(renderer2d, grid, fog, viewingPlayer,
                                             camera, screenWidth, screenHeight);
    }

    // Layer 1.6: City tile highlight overlay (when a city is selected, dim non-interactable tiles)
    // Find the selected city by searching player cities for a matching entity.
    // selectedEntity is retained for rendering selection state.
    {
        const aoc::game::City* selCity = nullptr;
        for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
            for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
                // Match by location: the selected entity's position corresponds to a city tile.
                // Since the object model does not use EntityId for cities, we identify the
                // selected city as the one whose tile was clicked (stored externally).
                // For now, skip the overlay when no entity is selected.
                (void)cityPtr;
            }
        }

        if (selCity != nullptr) {
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

            // Get viewing player's treasury for tile-buy affordability check
            CurrencyAmount playerTreasury = 0;
            const aoc::game::Player* viewerPlayer = gameState.player(viewingPlayer);
            if (viewerPlayer != nullptr) {
                playerTreasury = viewerPlayer->treasury();
            }

            for (int32_t row = minRow; row <= maxRow; ++row) {
                for (int32_t col = minCol; col <= maxCol; ++col) {
                    int32_t index = row * grid.width() + col;
                    aoc::hex::AxialCoord axial = aoc::hex::offsetToAxial({col, row});
                    float cx2 = 0.0f, cy2 = 0.0f;
                    aoc::hex::axialToPixel(axial, hexSize, cx2, cy2);
                    if (cx2 < tlx || cx2 > brx || cy2 < tly || cy2 > bry) { continue; }

                    aoc::map::TileVisibility vis = fog.visibility(viewingPlayer, index);
                    if (vis == aoc::map::TileVisibility::Unseen) { continue; }

                    int32_t dist = grid.distance(selCity->location(), axial);
                    bool isWorked = selCity->isTileWorked(axial);

                    bool isBuyable = (grid.owner(index) == INVALID_PLAYER);
                    if (isBuyable) {
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
                        renderer2d.drawFilledHexagon(cx2, cy2, hexW, hexH,
                                                     0.1f, 0.5f, 0.1f, 0.20f);
                        renderer2d.drawHexagonOutline(cx2, cy2, hexW * 0.95f, hexH * 0.95f,
                                                      0.3f, 0.8f, 0.3f, 0.5f, 1.5f);
                    } else if (isBuyable) {
                        int32_t tileCost = 25 * std::max(1, dist);
                        bool canAfford = (playerTreasury >= static_cast<CurrencyAmount>(tileCost));

                        if (canAfford) {
                            renderer2d.drawFilledHexagon(cx2, cy2, hexW, hexH,
                                                         0.1f, 0.4f, 0.1f, 0.25f);
                            renderer2d.drawHexagonOutline(cx2, cy2, hexW * 0.95f, hexH * 0.95f,
                                                          0.2f, 0.7f, 0.2f, 0.6f, 1.5f);
                        } else {
                            renderer2d.drawFilledHexagon(cx2, cy2, hexW, hexH,
                                                         0.4f, 0.1f, 0.1f, 0.25f);
                            renderer2d.drawHexagonOutline(cx2, cy2, hexW * 0.95f, hexH * 0.95f,
                                                          0.7f, 0.2f, 0.2f, 0.6f, 1.5f);
                        }
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
                        renderer2d.drawFilledHexagon(cx2, cy2, hexW, hexH,
                                                     0.0f, 0.0f, 0.0f, 0.4f);
                    }

                    bool isOwned = (grid.owner(index) == viewingPlayer);
                    if (isOwned && dist <= 3 && axial != selCity->location()) {
                        float circleR = hexSize * 0.18f;
                        float circleX = cx2;
                        float circleY = cy2 - hexSize * 0.25f;
                        bool isLocked = selCity->isTileLocked(axial);

                        if (isWorked) {
                            renderer2d.drawFilledCircle(circleX, circleY, circleR,
                                                         0.15f, 0.55f, 0.15f, 0.9f);
                            renderer2d.drawCircle(circleX, circleY, circleR,
                                                   0.3f, 0.8f, 0.3f, 0.9f, 1.5f);
                            float headR = circleR * 0.3f;
                            renderer2d.drawFilledCircle(circleX, circleY - headR * 0.6f, headR,
                                                         1.0f, 1.0f, 1.0f, 0.9f);
                            renderer2d.drawFilledArc(circleX, circleY + headR * 0.8f, headR * 1.5f,
                                                      3.14159f, 0.0f, 1.0f, 1.0f, 1.0f, 0.8f);
                        } else {
                            renderer2d.drawCircle(circleX, circleY, circleR,
                                                   0.5f, 0.5f, 0.5f, 0.5f, 1.0f);
                        }

                        if (isLocked) {
                            float lockX = circleX + circleR * 0.7f;
                            float lockY = circleY - circleR * 0.7f;
                            float lockS = circleR * 0.45f;
                            renderer2d.drawFilledRect(lockX - lockS * 0.5f, lockY,
                                                       lockS, lockS * 0.8f,
                                                       0.9f, 0.75f, 0.2f, 0.9f);
                            renderer2d.drawCircle(lockX, lockY, lockS * 0.4f,
                                                   0.9f, 0.75f, 0.2f, 0.9f, 1.5f);
                        }
                    }
                }
            }
        }
    }

    // Layer 2: Cities (world coordinates)
    this->m_unitRenderer.drawCities(renderer2d, gameState, fog, grid, viewingPlayer,
                                     camera, hexSize, screenWidth, screenHeight);

    // Layer 3: Units (world coordinates)
    this->m_unitRenderer.drawUnits(renderer2d, gameState, fog, grid, viewingPlayer,
                                    camera, hexSize, screenWidth, screenHeight);

    // Layer 3.1: Civ-6-style worker placement overlay. Highlights every
    // tile the selected city can work (owned + walkable, within 3 hexes
    // of the city centre) and marks worked tiles with a filled disc.
    // Click handling lives in Application; this layer only draws.
    if (this->workerOverlayCity != nullptr) {
        const aoc::game::City* wc = this->workerOverlayCity;
        const aoc::hex::AxialCoord ctr = wc->location();
        constexpr int OVERLAY_RADIUS = 3;
        for (int dq = -OVERLAY_RADIUS; dq <= OVERLAY_RADIUS; ++dq) {
            for (int dr = -OVERLAY_RADIUS; dr <= OVERLAY_RADIUS; ++dr) {
                const aoc::hex::AxialCoord t{ctr.q + dq, ctr.r + dr};
                if (aoc::hex::distance(ctr, t) > OVERLAY_RADIUS) { continue; }
                if (!grid.isValid(t)) { continue; }
                const int32_t idx = grid.toIndex(t);
                if (grid.movementCost(idx) == 0) { continue; }       // water/impassable
                if (grid.owner(idx) != wc->owner() && t != ctr) { continue; }

                float tcx = 0.0f, tcy = 0.0f;
                aoc::hex::axialToPixel(t, hexSize, tcx, tcy);
                float verts[12];
                aoc::hex::hexVertices(tcx, tcy, hexSize, verts);

                // Outline every workable tile in soft gold.
                for (int e = 0; e < 6; ++e) {
                    const float x1 = verts[e * 2];
                    const float y1 = verts[e * 2 + 1];
                    const float x2 = verts[((e + 1) % 6) * 2];
                    const float y2 = verts[((e + 1) % 6) * 2 + 1];
                    renderer2d.drawCapsule(x1, y1, x2, y2, 1.5f,
                                           1.0f, 0.85f, 0.40f, 0.55f, 0.0f);
                }

                // City centre always counts as worked, draw a star-like
                // ring around it for distinction.
                const bool isCenter = (t == ctr);
                bool isWorked = isCenter;
                if (!isCenter) {
                    for (const aoc::hex::AxialCoord& wt : wc->workedTiles()) {
                        if (wt == t) { isWorked = true; break; }
                    }
                }
                if (isWorked) {
                    // Filled bronze disc + gold ring = "worker assigned".
                    renderer2d.drawFilledRect(tcx - hexSize * 0.30f,
                                               tcy - hexSize * 0.30f,
                                               hexSize * 0.60f, hexSize * 0.60f,
                                               0.643f, 0.486f, 0.227f, 0.85f);
                    renderer2d.drawFilledRect(tcx - hexSize * 0.20f,
                                               tcy - hexSize * 0.20f,
                                               hexSize * 0.40f, hexSize * 0.40f,
                                               1.0f, 0.85f, 0.40f, 0.95f);
                } else {
                    // Hollow muted dot = "tile workable but free".
                    renderer2d.drawFilledRect(tcx - hexSize * 0.18f,
                                               tcy - hexSize * 0.18f,
                                               hexSize * 0.36f, hexSize * 0.36f,
                                               0.20f, 0.18f, 0.13f, 0.55f);
                }
            }
        }
    }

    // Layer 3.2: Selection highlight for the active unit or city.  Drawn as
    // a bright ring around the tile so the player always knows which entity
    // responds to the action panel hotkeys.
    if (this->hasSelection() && grid.isValid(this->selectionHighlight)) {
        float selCx = 0.0f, selCy = 0.0f;
        hex::axialToPixel(this->selectionHighlight, hexSize, selCx, selCy);
        float verts[12];
        hex::hexVertices(selCx, selCy, hexSize, verts);
        const float pulse = 0.70f + 0.30f * 0.5f;  // static glow; could oscillate if animated
        for (int e = 0; e < 6; ++e) {
            const float x1 = verts[e * 2];
            const float y1 = verts[e * 2 + 1];
            const float x2 = verts[((e + 1) % 6) * 2];
            const float y2 = verts[((e + 1) % 6) * 2 + 1];
            renderer2d.drawCapsule(x1, y1, x2, y2, 3.0f,
                                   1.0f, 0.92f, 0.35f, pulse, 0.0f);
        }
    }

    // Layer 3.5: City name labels (world-space text above each city hex)
    {
        const float invZoomLabel = 1.0f / camera.zoom();
        constexpr float LABEL_FONT_SIZE = 10.0f;
        const float labelOffsetY = hexSize * 0.60f;

        for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
            for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
                const aoc::game::City& city = *cityPtr;

                if (grid.isValid(city.location())) {
                    int32_t tileIdx = grid.toIndex(city.location());
                    aoc::map::TileVisibility vis = fog.visibility(viewingPlayer, tileIdx);
                    if (vis == aoc::map::TileVisibility::Unseen) {
                        continue;
                    }
                    if (city.owner() != viewingPlayer && vis != aoc::map::TileVisibility::Visible) {
                        continue;
                    }
                }

                float cityCx = 0.0f, cityCy = 0.0f;
                hex::axialToPixel(city.location(), hexSize, cityCx, cityCy);

                const aoc::ui::Rect textBounds =
                    aoc::ui::BitmapFont::measureText(city.name(), LABEL_FONT_SIZE);
                const float textWorldW = textBounds.w * invZoomLabel;
                const float textWorldH = textBounds.h * invZoomLabel;
                const float textX = cityCx - textWorldW * 0.5f;
                const float textY = cityCy - labelOffsetY - textWorldH * 0.5f;

                const std::size_t cIdx =
                    static_cast<std::size_t>(city.owner()) % LABEL_PLAYER_COLORS.size();
                const aoc::ui::Color labelColor{
                    LABEL_PLAYER_COLORS[cIdx][0],
                    LABEL_PLAYER_COLORS[cIdx][1],
                    LABEL_PLAYER_COLORS[cIdx][2],
                    1.0f};

                aoc::ui::BitmapFont::drawText(renderer2d, city.name(),
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

    // Layer 3.6: Ranged attack range overlay for the selected ranged unit.
    // The selected unit is located by searching all players for a unit whose
    // entity matches selectedEntity (legacy field kept for caller compatibility).
    {
        const aoc::game::Unit* selUnit = nullptr;
        for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
            for (const std::unique_ptr<aoc::game::Unit>& unitPtr : playerPtr->units()) {
                // selectedEntity is currently unused in the object model — skip the overlay
                // until callers migrate to tracking selection via Unit* directly.
                (void)unitPtr;
            }
        }
        if (selUnit != nullptr) {
            const aoc::sim::UnitTypeDef& selDef = selUnit->typeDef();
            if (selDef.rangedStrength > 0 && selDef.range > 0) {
                this->m_unitRenderer.drawRangedRange(renderer2d, gameState,
                                                      selUnit->position(), selDef.range,
                                                      selUnit->owner(), hexSize);
            }
        }
    }

    // Layer 4: UI overlay (single batch - transform UI to world-space so the
    // camera shader maps it back to screen-space correctly).
    float invZoom = 1.0f / camera.zoom();
    uiManager.layout();
    uiManager.transformBounds(topLeftX, topLeftY, invZoom);
    // Plumb the command buffer so `clipChildren` panels can push/pop
    // scissor rects during this render pass.
    uiManager.setRenderCommandBuffer(static_cast<void*>(commandBuffer));
    uiManager.render(renderer2d);
    uiManager.setRenderCommandBuffer(nullptr);
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

    // Minimap (transformed to world-space). Suppressed while a modal
    // screen is open — the world overview shouldn't peek through the
    // tech tree, etc. Dimensions come from the shared
    // `Minimap::computeRect` helper so the click-handler in
    // Application.cpp uses identical bounds.
    if (!this->m_minimapSuppressed) {
        const Minimap::Rect mmRect = Minimap::computeRect(grid, screenHeight);
        const float mmWorldX = topLeftX + mmRect.x * invZoom;
        const float mmWorldY = topLeftY + mmRect.y * invZoom;
        this->m_minimap.draw(renderer2d, grid, fog, viewingPlayer, camera,
                             mmWorldX, mmWorldY, mmRect.w * invZoom, mmRect.h * invZoom,
                             screenWidth, screenHeight);
    }

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
