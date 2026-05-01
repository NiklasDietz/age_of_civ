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
                // PlateBoundaries skips the fill pass entirely — only
                // borders are drawn, so collision-type colours read
                // clearly without competing against plate-id hues.
                // CrustAge / Sediment / RockType / Margins compute their
                // own per-tile colours (skip the cat-based path).
                float fillR = 0.0f, fillG = 0.0f, fillB = 0.0f, fillA = 0.45f;
                bool useFill = false;
                if (this->overlayMode == MapOverlay::CrustAge) {
                    const auto& ages = grid.crustAgeTile();
                    if (!ages.empty()) {
                        const float a = ages[static_cast<std::size_t>(index)];
                        // Red (young) → green → blue (ancient); cap age
                        // colouring at 200 epochs.
                        const float t = std::clamp(a / 200.0f, 0.0f, 1.0f);
                        fillR = 1.0f - t;
                        fillG = (t < 0.5f) ? (t * 2.0f) : (1.0f - (t - 0.5f) * 2.0f);
                        fillB = t;
                        fillA = 0.55f;
                        useFill = true;
                    }
                } else if (this->overlayMode == MapOverlay::Sediment) {
                    const auto& sed = grid.sedimentDepth();
                    if (!sed.empty()) {
                        const float s = sed[static_cast<std::size_t>(index)];
                        if (s > 0.005f) {
                            const float t = std::clamp(s / 0.20f, 0.0f, 1.0f);
                            // Yellow → orange → brown (deeper basins).
                            fillR = 1.0f;
                            fillG = 0.85f - 0.55f * t;
                            fillB = 0.10f * (1.0f - t);
                            fillA = 0.30f + 0.40f * t;
                            useFill = true;
                        }
                    }
                } else if (this->overlayMode == MapOverlay::RockType) {
                    const auto& rt = grid.rockType();
                    if (!rt.empty()) {
                        const uint8_t r = rt[static_cast<std::size_t>(index)];
                        switch (r) {
                            case 0: fillR = 0.95f; fillG = 0.85f; fillB = 0.55f; break; // sed: tan
                            case 1: fillR = 0.20f; fillG = 0.30f; fillB = 0.55f; break; // igneous: dark blue
                            case 2: fillR = 0.55f; fillG = 0.25f; fillB = 0.65f; break; // meta: purple
                            case 3: fillR = 0.10f; fillG = 0.85f; fillB = 0.25f; break; // ophiolite: bright green
                            default: break;
                        }
                        fillA = 0.55f;
                        useFill = true;
                    }
                } else if (this->overlayMode == MapOverlay::Margins) {
                    const auto& mt = grid.marginType();
                    if (!mt.empty()) {
                        const uint8_t m = mt[static_cast<std::size_t>(index)];
                        if (m == 1) {
                            // Active margin: bright red
                            fillR = 0.95f; fillG = 0.20f; fillB = 0.20f;
                            fillA = 0.55f; useFill = true;
                        } else if (m == 2) {
                            // Passive margin: bright blue
                            fillR = 0.20f; fillG = 0.45f; fillB = 0.95f;
                            fillA = 0.50f; useFill = true;
                        }
                    }
                } else if (this->overlayMode == MapOverlay::Volcanism) {
                    const auto& vc = grid.volcanism();
                    if (!vc.empty()) {
                        const uint8_t v = vc[static_cast<std::size_t>(index)];
                        switch (v) {
                            case 1: fillR = 1.0f;  fillG = 0.30f; fillB = 0.10f; useFill = true; break; // arc
                            case 2: fillR = 1.0f;  fillG = 0.55f; fillB = 0.0f;  useFill = true; break; // hotspot
                            case 3: fillR = 0.55f; fillG = 0.05f; fillB = 0.35f; useFill = true; break; // LIP
                            case 4: fillR = 0.10f; fillG = 0.85f; fillB = 0.55f; useFill = true; break; // rift volc
                            case 5: fillR = 1.0f;  fillG = 1.0f;  fillB = 0.20f; useFill = true; break; // hot spring
                            case 6: fillR = 0.85f; fillG = 0.85f; fillB = 0.85f; useFill = true; break; // inselberg
                            case 7: fillR = 0.95f; fillG = 0.90f; fillB = 0.60f; useFill = true; break; // dunes
                            default: break;
                        }
                        if (useFill) { fillA = 0.55f; }
                    }
                } else if (this->overlayMode == MapOverlay::Hazard) {
                    const auto& hz = grid.seismicHazard();
                    if (!hz.empty()) {
                        const uint8_t h = hz[static_cast<std::size_t>(index)];
                        const uint8_t lvl = h & 0x07;
                        const bool tsunami = (h & 0x08) != 0;
                        if (lvl > 0 || tsunami) {
                            fillR = 0.50f + 0.15f * static_cast<float>(lvl);
                            fillG = (lvl >= 3) ? 0.10f : 0.40f;
                            fillB = tsunami ? 0.85f : 0.10f;
                            fillA = 0.55f; useFill = true;
                        }
                    }
                } else if (this->overlayMode == MapOverlay::Soil) {
                    const auto& sf = grid.soilFertility();
                    if (!sf.empty()) {
                        const float s = sf[static_cast<std::size_t>(index)];
                        // Brown (poor) → green (rich)
                        fillR = 0.45f + 0.15f * (1.0f - s);
                        fillG = 0.30f + 0.55f * s;
                        fillB = 0.20f * (1.0f - s);
                        fillA = 0.55f; useFill = true;
                    }
                } else if (this->overlayMode == MapOverlay::Hazards) {
                    const auto& nh = grid.naturalHazard();
                    const std::size_t si = static_cast<std::size_t>(index);
                    if (!nh.empty() && si < nh.size() && nh[si] != 0) {
                        const uint16_t h = nh[si];
                        if      (h & 0x0001) { fillR=1.0f; fillG=0.30f; fillB=0.0f; }
                        else if (h & 0x0002) { fillR=0.10f; fillG=0.55f; fillB=0.95f; }
                        else if (h & 0x0004) { fillR=0.85f; fillG=0.70f; fillB=0.30f; }
                        else if (h & 0x0008) { fillR=0.95f; fillG=0.95f; fillB=0.95f; }
                        else if (h & 0x0010) { fillR=0.70f; fillG=0.40f; fillB=0.20f; }
                        else if (h & 0x0020) { fillR=0.55f; fillG=0.30f; fillB=0.55f; }
                        else if (h & 0x0040) { fillR=0.30f; fillG=0.20f; fillB=0.10f; }
                        else if (h & 0x0080) { fillR=0.30f; fillG=0.10f; fillB=0.30f; }
                        else if (h & 0x0100) { fillR=0.20f; fillG=0.20f; fillB=0.95f; }
                        else if (h & 0x0200) { fillR=0.85f; fillG=0.55f; fillB=0.10f; }
                        fillA = 0.55f; useFill = true;
                    }
                } else if (this->overlayMode == MapOverlay::BiomeSub) {
                    const auto& bs = grid.biomeSubtype();
                    const std::size_t si = static_cast<std::size_t>(index);
                    if (!bs.empty() && si < bs.size() && bs[si] != 0) {
                        const uint8_t b = bs[si];
                        const float h = static_cast<float>(b) / 14.0f;
                        fillR = 0.4f + 0.5f * std::cos(h * 6.2832f);
                        fillG = 0.4f + 0.5f * std::cos((h + 0.33f) * 6.2832f);
                        fillB = 0.4f + 0.5f * std::cos((h + 0.66f) * 6.2832f);
                        fillA = 0.55f; useFill = true;
                    }
                } else if (this->overlayMode == MapOverlay::MarineDepth) {
                    const auto& md = grid.marineDepth();
                    const std::size_t si = static_cast<std::size_t>(index);
                    if (!md.empty() && si < md.size() && md[si] != 0) {
                        const uint8_t z = md[si];
                        const float t = static_cast<float>(z) / 6.0f;
                        fillR = 0.10f * (1.0f - t);
                        fillG = 0.30f + 0.40f * (1.0f - t);
                        fillB = 0.50f + 0.45f * t;
                        fillA = 0.55f; useFill = true;
                    }
                } else if (this->overlayMode == MapOverlay::Wildlife) {
                    const auto& wl = grid.wildlife();
                    const std::size_t si = static_cast<std::size_t>(index);
                    if (!wl.empty() && si < wl.size() && wl[si] != 0) {
                        switch (wl[si]) {
                            case 1: fillR=0.85f; fillG=0.60f; fillB=0.20f; break;
                            case 2: fillR=0.55f; fillG=0.35f; fillB=0.20f; break;
                            case 3: fillR=0.20f; fillG=0.55f; fillB=0.85f; break;
                            case 4: fillR=0.95f; fillG=0.30f; fillB=0.55f; break;
                            case 5: fillR=0.55f; fillG=0.95f; fillB=0.30f; break;
                            default: break;
                        }
                        fillA = 0.55f; useFill = true;
                    }
                } else if (this->overlayMode == MapOverlay::Disease) {
                    const auto& ds = grid.disease();
                    const std::size_t si = static_cast<std::size_t>(index);
                    if (!ds.empty() && si < ds.size() && ds[si] != 0) {
                        fillR = 0.65f; fillG = 0.20f; fillB = 0.65f;
                        fillA = std::min(0.65f,
                            0.30f + 0.10f * static_cast<float>(__builtin_popcount(ds[si])));
                        useFill = true;
                    }
                } else if (this->overlayMode == MapOverlay::EnergyWind
                        || this->overlayMode == MapOverlay::EnergySolar
                        || this->overlayMode == MapOverlay::EnergyHydro
                        || this->overlayMode == MapOverlay::EnergyGeothermal
                        || this->overlayMode == MapOverlay::EnergyTidal
                        || this->overlayMode == MapOverlay::EnergyWave) {
                    const std::vector<uint8_t>* en = nullptr;
                    if (this->overlayMode == MapOverlay::EnergyWind)        { en = &grid.windEnergy(); }
                    else if (this->overlayMode == MapOverlay::EnergySolar)  { en = &grid.solarEnergy(); }
                    else if (this->overlayMode == MapOverlay::EnergyHydro)  { en = &grid.hydroEnergy(); }
                    else if (this->overlayMode == MapOverlay::EnergyGeothermal) { en = &grid.geothermalEnergy(); }
                    else if (this->overlayMode == MapOverlay::EnergyTidal)  { en = &grid.tidalEnergy(); }
                    else                                                    { en = &grid.waveEnergy(); }
                    const std::size_t si = static_cast<std::size_t>(index);
                    if (en && !en->empty() && si < en->size()) {
                        const float v = static_cast<float>((*en)[si]) / 255.0f;
                        fillR = v;
                        fillG = 0.85f * v;
                        fillB = 0.20f * (1.0f - v);
                        fillA = 0.20f + 0.40f * v;
                        useFill = true;
                    }
                } else if (this->overlayMode == MapOverlay::AtmExtras) {
                    const auto& ae = grid.atmosphericExtras();
                    const std::size_t si = static_cast<std::size_t>(index);
                    if (!ae.empty() && si < ae.size() && ae[si] != 0) {
                        const uint8_t v = ae[si];
                        if      (v & 0x01) { fillR=0.95f; fillG=0.85f; fillB=0.20f; }
                        else if (v & 0x02) { fillR=0.20f; fillG=0.85f; fillB=0.95f; }
                        else if (v & 0x04) { fillR=0.85f; fillG=0.20f; fillB=0.20f; }
                        else if (v & 0x08) { fillR=0.20f; fillG=0.20f; fillB=0.95f; }
                        else if (v & 0x10) { fillR=0.95f; fillG=0.55f; fillB=0.20f; }
                        else if (v & 0x20) { fillR=0.20f; fillG=0.95f; fillB=0.55f; }
                        fillA = 0.50f; useFill = true;
                    }
                } else if (this->overlayMode == MapOverlay::HydroExtras) {
                    const auto& he = grid.hydroExtras();
                    const std::size_t si = static_cast<std::size_t>(index);
                    if (!he.empty() && si < he.size() && he[si] != 0) {
                        const uint8_t v = he[si];
                        if      (v & 0x40) { fillR=0.85f; fillG=0.85f; fillB=0.10f; } // salt lake
                        else if (v & 0x80) { fillR=0.30f; fillG=0.55f; fillB=0.95f; } // fresh lake
                        else if (v & 0x04) { fillR=0.95f; fillG=0.30f; fillB=0.30f; } // crater
                        else if (v & 0x10) { fillR=0.55f; fillG=0.85f; fillB=0.95f; } // thermokarst
                        else if (v & 0x20) { fillR=0.65f; fillG=0.95f; fillB=0.65f; } // oxbow
                        else if (v & 0x02) { fillR=0.20f; fillG=0.95f; fillB=0.95f; } // spring
                        else if (v & 0x01) { fillR=0.20f; fillG=0.55f; fillB=0.55f; } // aquifer
                        fillA = 0.55f; useFill = true;
                    }
                } else if (this->overlayMode == MapOverlay::Events) {
                    const auto& ev = grid.eventMarker();
                    const std::size_t si = static_cast<std::size_t>(index);
                    if (!ev.empty() && si < ev.size() && ev[si] != 0) {
                        switch (ev[si]) {
                            case 1: fillR=0.95f; fillG=0.20f; fillB=0.20f; break; // eruption
                            case 2: fillR=0.85f; fillG=0.65f; fillB=0.20f; break; // crater
                            case 3: fillR=0.95f; fillG=0.10f; fillB=0.95f; break; // supervolcano
                            default: break;
                        }
                        fillA = 0.85f; useFill = true;
                    }
                } else if (this->overlayMode == MapOverlay::Storms) {
                    const auto& ch = grid.climateHazard();
                    const std::size_t si = static_cast<std::size_t>(index);
                    if (!ch.empty() && si < ch.size()) {
                        const uint8_t f = ch[si];
                        if ((f & 0x01) != 0) {
                            // Hurricane belt — orange
                            fillR = 1.0f; fillG = 0.55f; fillB = 0.10f;
                            fillA = 0.55f; useFill = true;
                        } else if ((f & 0x02) != 0) {
                            // Tornado alley — red
                            fillR = 0.95f; fillG = 0.10f; fillB = 0.20f;
                            fillA = 0.55f; useFill = true;
                        } else if ((f & 0x04) != 0) {
                            // Storm track — light blue
                            fillR = 0.30f; fillG = 0.55f; fillB = 0.95f;
                            fillA = 0.50f; useFill = true;
                        } else if ((f & 0x08) != 0) {
                            // Jet stream — purple
                            fillR = 0.55f; fillG = 0.20f; fillB = 0.85f;
                            fillA = 0.40f; useFill = true;
                        }
                    }
                } else if (this->overlayMode == MapOverlay::Glacial) {
                    const auto& gf = grid.glacialFeature();
                    const std::size_t si = static_cast<std::size_t>(index);
                    if (!gf.empty() && si < gf.size()) {
                        switch (gf[si]) {
                            case 1: fillR = 0.85f; fillG = 0.85f; fillB = 0.50f; useFill = true; break; // moraine
                            case 2: fillR = 0.40f; fillG = 0.65f; fillB = 0.95f; useFill = true; break; // U-valley
                            case 3: fillR = 0.30f; fillG = 0.30f; fillB = 0.30f; useFill = true; break; // cave
                            case 4: fillR = 0.65f; fillG = 0.50f; fillB = 0.30f; useFill = true; break; // drumlin
                            case 5: fillR = 0.50f; fillG = 0.85f; fillB = 0.65f; useFill = true; break; // esker
                            default: break;
                        }
                        if (useFill) { fillA = 0.55f; }
                    }
                } else if (this->overlayMode == MapOverlay::Ocean) {
                    const auto& oz = grid.oceanZone();
                    const std::size_t si = static_cast<std::size_t>(index);
                    if (!oz.empty() && si < oz.size()) {
                        const uint8_t v = oz[si];
                        const uint8_t tidal = v & 0x03;
                        const uint8_t salin = (v >> 2) & 0x03;
                        if (tidal != 0 || salin != 1) {
                            // Tidal in red channel, salinity in blue.
                            fillR = 0.20f + 0.25f * static_cast<float>(tidal);
                            fillG = 0.45f;
                            fillB = (salin == 2) ? 0.20f
                                  : (salin == 3) ? 0.95f
                                  : (salin == 0) ? 0.70f : 0.55f;
                            fillA = 0.55f; useFill = true;
                        }
                    }
                } else if (this->overlayMode == MapOverlay::Clouds) {
                    const auto& cc = grid.cloudCover();
                    const std::size_t si = static_cast<std::size_t>(index);
                    if (!cc.empty() && si < cc.size()) {
                        const float c = cc[si];
                        fillR = 0.85f; fillG = 0.85f; fillB = 0.95f;
                        fillA = std::clamp(c * 0.55f, 0.0f, 0.55f);
                        useFill = true;
                    }
                } else if (this->overlayMode == MapOverlay::Flow) {
                    const auto& fl = grid.flowDir();
                    const std::size_t si = static_cast<std::size_t>(index);
                    if (!fl.empty() && si < fl.size()) {
                        const uint8_t d = fl[si];
                        if (d == 0xFFu) {
                            // Sink / endorheic — black
                            fillR = 0.0f; fillG = 0.0f; fillB = 0.0f;
                        } else {
                            // Color by direction (0-5) hue ramp
                            const float h = static_cast<float>(d) / 6.0f;
                            fillR = 0.4f + 0.5f * std::cos(h * 6.2832f);
                            fillG = 0.4f + 0.5f * std::cos((h + 0.33f) * 6.2832f);
                            fillB = 0.4f + 0.5f * std::cos((h + 0.66f) * 6.2832f);
                        }
                        fillA = 0.50f; useFill = true;
                    }
                } else if (this->overlayMode == MapOverlay::Realms) {
                    const auto& iso = grid.isolatedRealm();
                    const auto& bri = grid.landBridge();
                    const auto& ref = grid.refugium();
                    const std::size_t si = static_cast<std::size_t>(index);
                    if (!iso.empty() && si < iso.size() && iso[si] != 0) {
                        // Magenta — biogeographically isolated continent
                        fillR = 0.85f; fillG = 0.20f; fillB = 0.85f;
                        fillA = 0.55f; useFill = true;
                    } else if (!bri.empty() && si < bri.size() && bri[si] != 0) {
                        // Cyan — potential land bridge
                        fillR = 0.20f; fillG = 0.85f; fillB = 0.95f;
                        fillA = 0.65f; useFill = true;
                    } else if (!ref.empty() && si < ref.size() && ref[si] != 0) {
                        // Yellow-green — glacial refugium
                        fillR = 0.65f; fillG = 0.95f; fillB = 0.20f;
                        fillA = 0.55f; useFill = true;
                    }
                }
                if (useFill) {
                    const aoc::hex::AxialCoord axialF =
                        aoc::hex::offsetToAxial({col, row});
                    float fx = 0.0f, fy = 0.0f;
                    aoc::hex::axialToPixel(axialF, hexSizeOv, fx, fy);
                    renderer2d.drawFilledHexagon(fx, fy,
                        hexSizeOv * 0.866f, hexSizeOv,
                        fillR, fillG, fillB, fillA);
                    continue;
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

        // Pass 2: draw black borders along edges where neighbour belongs to
        // a different category. Mirrors the in-game government / religion
        // overlay treatment so plate boundaries read at a glance.
        // edge i (vertices i → (i+1)%6) maps to neighbour DIR via:
        //   edge 0 → SE(5), 1 → SW(4), 2 → W(3), 3 → NW(2), 4 → NE(1), 5 → E(0).
        constexpr int32_t EDGE_TO_DIR[6] = {5, 4, 3, 2, 1, 0};
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
                uint8_t myCat = 0xFF;
                if (this->overlayMode == MapOverlay::TectonicPlates
                    || this->overlayMode == MapOverlay::PlateBoundaries) {
                    myCat = grid.plateId(index);
                }
                if (myCat == 0xFFu) { continue; }

                const aoc::hex::AxialCoord axial =
                    aoc::hex::offsetToAxial({col, row});
                float cx = 0.0f, cy = 0.0f;
                aoc::hex::axialToPixel(axial, hexSizeOv, cx, cy);
                float verts[12];
                aoc::hex::hexVertices(cx, cy, hexSizeOv, verts);
                const std::array<aoc::hex::AxialCoord, 6> nbrs =
                    aoc::hex::neighbors(axial);
                for (int32_t edge = 0; edge < 6; ++edge) {
                    const aoc::hex::AxialCoord nb = nbrs[
                        static_cast<std::size_t>(EDGE_TO_DIR[edge])];
                    bool drawEdge = false;
                    if (!grid.isValid(nb)) {
                        drawEdge = true; // map edge counts as boundary
                    } else {
                        const int32_t nIdx = grid.toIndex(nb);
                        uint8_t nbCat = 0xFF;
                        if (this->overlayMode == MapOverlay::TectonicPlates
                            || this->overlayMode == MapOverlay::PlateBoundaries) {
                            nbCat = grid.plateId(nIdx);
                        }
                        if (nbCat != myCat) { drawEdge = true; }
                    }
                    if (!drawEdge) { continue; }
                    const float ax = verts[edge * 2];
                    const float ay = verts[edge * 2 + 1];
                    const float bx = verts[((edge + 1) % 6) * 2];
                    const float by = verts[((edge + 1) % 6) * 2 + 1];
                    // Boundary type colour. Lookup neighbour plate id;
                    // compute relative velocity along boundary normal
                    // (= convergence/divergence) vs tangent (= transform).
                    //   |normal|  ≫ |tangent|  → convergent (red)
                    //                            or divergent (blue)
                    //   |tangent| ≫ |normal|  → transform (yellow)
                    //   both small             → passive (black)
                    float r = 0.0f, g = 0.0f, b = 0.0f;
                    uint8_t nbCatLine = 0xFFu;
                    if (grid.isValid(nbrs[static_cast<std::size_t>(EDGE_TO_DIR[edge])])) {
                        nbCatLine = grid.plateId(grid.toIndex(
                            nbrs[static_cast<std::size_t>(EDGE_TO_DIR[edge])]));
                    }
                    const auto& motions = grid.plateMotions();
                    const auto& centers = grid.plateCenters();
                    const auto& landFr  = grid.plateLandFrac();
                    if (myCat < motions.size() && nbCatLine < motions.size()
                        && nbCatLine != 0xFFu) {
                        const std::pair<float, float>& vA = motions[myCat];
                        const std::pair<float, float>& vB = motions[nbCatLine];
                        const std::pair<float, float>& cA = centers[myCat];
                        const std::pair<float, float>& cB = centers[nbCatLine];
                        float bnx = cB.first  - cA.first;
                        float bny = cB.second - cA.second;
                        const float bnLen = std::sqrt(bnx * bnx + bny * bny);
                        if (bnLen > 1e-4f) {
                            bnx /= bnLen; bny /= bnLen;
                            const float relVx = vA.first  - vB.first;
                            const float relVy = vA.second - vB.second;
                            const float normProj = relVx * bnx + relVy * bny;
                            const float tangProj = -relVx * bny + relVy * bnx;
                            const float aN = std::abs(normProj);
                            const float aT = std::abs(tangProj);
                            // Looser threshold (0.02 vs 0.05) — boundaries
                            // with weak relative motion still register.
                            // Real plates rarely sit exactly still.
                            if (aN > aT && aN > 0.02f) {
                                if (normProj > 0.0f) {
                                    // Plates closing → split convergent
                                    // into 3 sub-types using landFraction:
                                    //   both continental → collision (red)
                                    //   one ocean / one cont → subduction
                                    //     of ocean under cont (magenta)
                                    //   both oceanic → ocean subduction
                                    //     forms island arc (orange)
                                    const float lA = (myCat < landFr.size())
                                        ? landFr[myCat] : 0.5f;
                                    const float lB = (nbCatLine < landFr.size())
                                        ? landFr[nbCatLine] : 0.5f;
                                    const bool aLand = lA > 0.4f;
                                    const bool bLand = lB > 0.4f;
                                    if (aLand && bLand) {
                                        r = 0.95f; g = 0.10f; b = 0.10f; // collision red
                                    } else if (aLand != bLand) {
                                        r = 0.95f; g = 0.20f; b = 0.85f; // subduction magenta
                                    } else {
                                        r = 1.0f; g = 0.55f; b = 0.10f;  // ocean-ocean orange
                                    }
                                } else {
                                    // Plates pulling apart → divergent (blue).
                                    r = 0.10f; g = 0.40f; b = 0.95f;
                                }
                            } else if (aT > 0.02f) {
                                // Sliding past each other → transform (yellow).
                                r = 1.0f; g = 0.85f; b = 0.10f;
                            } else {
                                // Negligible relative motion → passive (grey).
                                r = 0.4f; g = 0.4f; b = 0.4f;
                            }
                        }
                    }
                    // Thick double-stroke border. PlateBoundaries mode
                    // uses heavier strokes (no per-tile fill underneath
                    // means borders carry the entire visual signal).
                    const bool boundariesOnly = (this->overlayMode
                        == MapOverlay::PlateBoundaries);
                    const float haloWidth   = boundariesOnly ? 26.0f : 16.0f;
                    const float colorWidth  = boundariesOnly ? 18.0f : 10.0f;
                    renderer2d.drawLine(ax, ay, bx, by,
                                         0.0f, 0.0f, 0.0f, 0.95f, haloWidth);
                    renderer2d.drawLine(ax, ay, bx, by,
                                         r, g, b, 1.0f, colorWidth);
                }
            }
        }
    }

    // Wind overlay: thick arrows on land tiles showing prevailing wind
    // direction by latitude band. Easterlies (trade + polar) blow E→W,
    // westerlies blow W→E. Renders ALL tiles to make it obvious; thick
    // lines + bright colours + filled arrowhead so it can't be missed.
    if (this->overlayMode == MapOverlay::Winds) {
        const float hexSizeOv = this->m_mapRenderer.hexSize();
        const int32_t width  = grid.width();
        const int32_t height = grid.height();
        for (int32_t row = 0; row < height; row += 3) {
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            const bool easterly = (lat < 0.30f) || (lat >= 0.60f);
            // Arrow points in wind-FLOW direction (where the wind blows TO).
            // Easterly = blows from east to west = arrow points -x.
            // Westerly = blows from west to east = arrow points +x.
            const float dirX = easterly ? -1.0f : +1.0f;
            // Bright colour band per zone.
            float r = 0.30f, g = 0.55f, b = 1.0f; // westerly (cool blue)
            if (lat < 0.30f)      { r = 1.0f;  g = 0.55f; b = 0.10f; } // trade (warm orange)
            else if (lat >= 0.60f){ r = 0.95f; g = 0.95f; b = 1.0f;  } // polar (bright white)
            for (int32_t col = 0; col < width; col += 4) {
                const aoc::hex::AxialCoord ax =
                    aoc::hex::offsetToAxial({col, row});
                float cx = 0.0f, cy = 0.0f;
                aoc::hex::axialToPixel(ax, hexSizeOv, cx, cy);
                const float L = hexSizeOv * 2.4f;
                // Shaft.
                const float x0 = cx - dirX * L * 0.5f;
                const float x1 = cx + dirX * L * 0.5f;
                renderer2d.drawLine(x0, cy, x1, cy, r, g, b, 1.0f, 9.0f);
                // Arrowhead: two strokes from tip backward + outward.
                const float head = L * 0.35f;
                const float headBack = -dirX * head; // back along shaft
                const float headSide = head * 0.6f;
                renderer2d.drawLine(x1, cy,
                    x1 + headBack, cy - headSide, r, g, b, 1.0f, 9.0f);
                renderer2d.drawLine(x1, cy,
                    x1 + headBack, cy + headSide, r, g, b, 1.0f, 9.0f);
            }
        }
    }

    // Ocean-current overlay: arrows on ALL water tiles showing gyre
    // circulation. Each tile's flow direction is the sum of
    //   1) base gyre flow per latitude band (Coriolis-driven)
    //   2) coast-deflection: parallel-to-coast push when near land
    // The result curves currents along continent edges, producing
    // gyre-like patterns around landmasses (Gulf Stream/N Atlantic
    // Drift/Canary/N Equatorial loop visible in the overlay).
    if (this->overlayMode == MapOverlay::OceanCurrents) {
        const float hexSizeOv = this->m_mapRenderer.hexSize();
        const int32_t width  = grid.width();
        const int32_t height = grid.height();
        for (int32_t row = 1; row < height; row += 3) {
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            const bool northern = (ny < 0.5f);
            for (int32_t col = 0; col < width; col += 3) {
                const int32_t idx = row * width + col;
                const aoc::map::TerrainType t = grid.terrain(idx);
                if (!aoc::map::isWater(t)) { continue; }

                // 1) Base gyre flow by latitude band. Subtropical NH
                //    gyres rotate clockwise: westward at low lat,
                //    poleward on west side of basin (right-hand part
                //    of an N-hemi gyre), eastward at higher lat,
                //    equatorward on east side. We approximate each
                //    tile's flow with a per-band rotational vector.
                float fx = 0.0f, fy = 0.0f;
                bool warm = false;
                if (lat < 0.10f) {
                    // Equatorial counter-current — eastward.
                    fx = +1.0f; warm = true;
                } else if (lat < 0.32f) {
                    // Tropical (trade-wind driven) — westward.
                    fx = -1.0f; warm = true;
                } else if (lat < 0.60f) {
                    // Mid-lat (westerlies-driven) — eastward.
                    fx = +1.0f; warm = false;
                } else {
                    // Sub-polar — slow westward.
                    fx = -0.7f; warm = false;
                }

                // 2) Coast deflection. Scan up to 6 tiles in 4 cardinal
                //    directions for the nearest land. If land is close,
                //    deflect parallel to coastline (90° rotation of
                //    the toward-land vector). Strength scales with
                //    proximity (linear falloff over scan distance).
                int32_t distE = 99, distW = 99, distN = 99, distS = 99;
                for (int32_t s = 1; s <= 6; ++s) {
                    const int32_t cE = col + s;
                    if (cE < width && !aoc::map::isWater(grid.terrain(row * width + cE))) {
                        distE = std::min(distE, s);
                    }
                    const int32_t cW = col - s;
                    if (cW >= 0 && !aoc::map::isWater(grid.terrain(row * width + cW))) {
                        distW = std::min(distW, s);
                    }
                    const int32_t rN = row - s;
                    if (rN >= 0 && !aoc::map::isWater(grid.terrain(rN * width + col))) {
                        distN = std::min(distN, s);
                    }
                    const int32_t rS = row + s;
                    if (rS < height && !aoc::map::isWater(grid.terrain(rS * width + col))) {
                        distS = std::min(distS, s);
                    }
                }
                // Strength proportional to land-proximity. Land east of
                // us → ocean here pushed parallel to that coast (along
                // ±y), with sign chosen by gyre direction.
                auto prox = [](int32_t d) {
                    return (d >= 6) ? 0.0f : 1.0f - static_cast<float>(d) / 6.0f;
                };
                const float pE = prox(distE);
                const float pW = prox(distW);
                const float pN = prox(distN);
                const float pS = prox(distS);
                // Land east of tile = WESTERN boundary of an ocean basin
                // (east coast of the continent the OCEAN is east of —
                // wait, this is the WEST coast of the next continent
                // looking east). For gyres: the western boundary
                // current (Gulf Stream, Kuroshio) is the FAST narrow
                // poleward current. So if land is east of us, we're
                // not on the gyre's western boundary.
                //
                // Reframing: gyre = closed loop around a basin centre.
                //   • Western boundary (east-coast of continent west
                //     of the basin) → narrow + STRONG + warm + poleward
                //   • Eastern boundary (west-coast of continent east of
                //     the basin) → broad + WEAK + cold + equatorward.
                // From this tile's perspective:
                //   • If LAND lies to the WEST → this is the gyre's
                //     western boundary current → strong, warm, poleward.
                //   • If LAND lies to the EAST → this is the gyre's
                //     eastern boundary → weak, cold, equatorward.
                if (pW > 0.0f) {
                    // Western boundary (Gulf-Stream-style). Strong
                    // narrow poleward warm current.
                    if (lat < 0.60f) {
                        fy += (northern ? -1.0f : +1.0f) * pW * 2.5f; warm = true;
                    } else {
                        fy += (northern ? -1.0f : +1.0f) * pW * 1.0f; warm = true;
                    }
                    // Strong poleward push outweighs horizontal.
                    fx *= 0.3f;
                }
                if (pE > 0.0f) {
                    // Eastern boundary (Canary-style). Weak broad
                    // equatorward cold current.
                    if (lat < 0.60f) {
                        fy += (northern ? +1.0f : -1.0f) * pE * 1.0f; warm = false;
                    } else {
                        fy += (northern ? +1.0f : -1.0f) * pE * 0.6f; warm = false;
                    }
                    fx *= 0.6f;
                }
                // Land to north/south: deflect parallel to coast.
                if (pN > 0.0f) {
                    fx -= pN * 0.6f;
                }
                if (pS > 0.0f) {
                    fx += pS * 0.6f;
                }

                // Coriolis deflection. Moving water in NH is deflected
                // right (clockwise rotation), in SH left (counter-clockwise).
                // Magnitude scales with latitude (≈ sin(lat) — strongest
                // at poles, zero at equator). Rotates each tile's flow
                // vector by a small angle, producing the gyre curl.
                {
                    const float coriolisAngle = (northern ? -1.0f : +1.0f)
                                              * lat * 0.35f; // ~20° max
                    const float cs = std::cos(coriolisAngle);
                    const float sn = std::sin(coriolisAngle);
                    const float nfx = fx * cs - fy * sn;
                    const float nfy = fx * sn + fy * cs;
                    fx = nfx; fy = nfy;
                }

                // Normalise.
                const float fLen = std::sqrt(fx * fx + fy * fy);
                if (fLen < 0.05f) { continue; }
                fx /= fLen; fy /= fLen;

                float r = 0.30f, g = 0.50f, b = 1.0f;
                if (warm) { r = 1.0f; g = 0.45f; b = 0.10f; }
                const aoc::hex::AxialCoord axc =
                    aoc::hex::offsetToAxial({col, row});
                float cx = 0.0f, cy = 0.0f;
                aoc::hex::axialToPixel(axc, hexSizeOv, cx, cy);
                const float L = hexSizeOv * 2.0f;
                const float x0 = cx - fx * L * 0.5f;
                const float y0 = cy - fy * L * 0.5f;
                const float x1 = cx + fx * L * 0.5f;
                const float y1 = cy + fy * L * 0.5f;
                renderer2d.drawLine(x0, y0, x1, y1, r, g, b, 1.0f, 9.0f);
                // Arrowhead — perpendicular from tip backward.
                const float head = L * 0.30f;
                const float headBackX = -fx * head;
                const float headBackY = -fy * head;
                const float perpX = -fy * head * 0.5f;
                const float perpY =  fx * head * 0.5f;
                renderer2d.drawLine(x1, y1,
                    x1 + headBackX + perpX, y1 + headBackY + perpY,
                    r, g, b, 1.0f, 9.0f);
                renderer2d.drawLine(x1, y1,
                    x1 + headBackX - perpX, y1 + headBackY - perpY,
                    r, g, b, 1.0f, 9.0f);
            }
        }
    }

    // Plate motion overlay: arrow at each plate centre showing its
    // velocity vector. Length proportional to speed, hue from
    // deterministic plate-id colour.
    if (this->overlayMode == MapOverlay::PlateMotion) {
        const float hexSizeOv = this->m_mapRenderer.hexSize();
        constexpr float SQRT3 = 1.7320508075688772f;
        const float worldW = static_cast<float>(grid.width()) * SQRT3 * hexSizeOv;
        const float worldH = static_cast<float>(grid.height()) * 1.5f * hexSizeOv;
        const auto& centers = grid.plateCenters();
        const auto& motions = grid.plateMotions();
        for (std::size_t i = 0; i < centers.size() && i < motions.size(); ++i) {
            const float wx = centers[i].first  * worldW;
            const float wy = centers[i].second * worldH;
            const float vx = motions[i].first;
            const float vy = motions[i].second;
            const float vLen = std::sqrt(vx * vx + vy * vy);
            if (vLen < 1e-3f) { continue; }
            const float vnx = vx / vLen;
            const float vny = vy / vLen;
            // Arrow length scales with speed (capped). Big at fast plates.
            const float L = hexSizeOv * (3.0f + std::min(vLen, 1.0f) * 8.0f);
            const float ax = wx;
            const float ay = wy;
            const float bx = wx + vnx * L;
            const float by = wy + vny * L;
            // Plate hue (deterministic from id).
            const uint32_t hh = static_cast<uint32_t>(i + 1) * 2654435761u;
            const float r = std::min(1.0f,
                static_cast<float>((hh >> 0)  & 0xFFu) / 255.0f * 0.7f + 0.3f);
            const float g = std::min(1.0f,
                static_cast<float>((hh >> 8)  & 0xFFu) / 255.0f * 0.7f + 0.3f);
            const float b = std::min(1.0f,
                static_cast<float>((hh >> 16) & 0xFFu) / 255.0f * 0.7f + 0.3f);
            renderer2d.drawLine(ax, ay, bx, by, r, g, b, 1.0f, 9.0f);
            // Filled arrowhead.
            const float head = L * 0.30f;
            const float perpX = -vny * head * 0.5f;
            const float perpY =  vnx * head * 0.5f;
            const float backX = bx - vnx * head;
            const float backY = by - vny * head;
            renderer2d.drawLine(bx, by, backX + perpX, backY + perpY, r, g, b, 1.0f, 9.0f);
            renderer2d.drawLine(bx, by, backX - perpX, backY - perpY, r, g, b, 1.0f, 9.0f);
            // Tail dot at plate centre for clarity.
            renderer2d.drawFilledCircle(wx, wy, hexSizeOv * 0.6f,
                                         r, g, b, 1.0f);
        }
    }

    // Hotspots overlay: dark red dot at each mantle plume position.
    if (this->overlayMode == MapOverlay::Hotspots) {
        const float hexSizeOv = this->m_mapRenderer.hexSize();
        constexpr float SQRT3 = 1.7320508075688772f;
        const float worldW = static_cast<float>(grid.width()) * SQRT3 * hexSizeOv;
        const float worldH = static_cast<float>(grid.height()) * 1.5f * hexSizeOv;
        for (const std::pair<float, float>& hs : grid.hotspots()) {
            const float wx = hs.first  * worldW;
            const float wy = hs.second * worldH;
            // Filled dark-red disk + thin black ring.
            renderer2d.drawFilledCircle(wx, wy, hexSizeOv * 0.6f,
                                         0.55f, 0.0f, 0.0f, 0.95f);
            renderer2d.drawCircle(wx, wy, hexSizeOv * 0.6f,
                                   0.0f, 0.0f, 0.0f, 1.0f, 1.5f);
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
        Minimap::Rect mmRect = Minimap::computeRect(grid, screenHeight);
        mmRect.y -= this->m_minimapBottomOffset;
        const float mmWorldX = topLeftX + mmRect.x * invZoom;
        const float mmWorldY = topLeftY + mmRect.y * invZoom;
        const bool platesOnMinimap =
            (this->overlayMode == MapOverlay::TectonicPlates);
        this->m_minimap.draw(renderer2d, grid, fog, viewingPlayer, camera,
                             mmWorldX, mmWorldY, mmRect.w * invZoom, mmRect.h * invZoom,
                             screenWidth, screenHeight,
                             platesOnMinimap);
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
