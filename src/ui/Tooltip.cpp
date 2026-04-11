/**
 * @file Tooltip.cpp
 * @brief Hover tooltip implementation: tile info, unit info, city info.
 */

#include "aoc/ui/Tooltip.hpp"
#include "aoc/ui/BitmapFont.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/FogOfWar.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/render/CameraController.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/unit/Combat.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/CityLoyalty.hpp"
#include "aoc/simulation/city/Happiness.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/tech/TechTree.hpp"

#include <renderer/Renderer2D.hpp>

#include <cstdio>

namespace aoc::ui {

void TooltipManager::update(float mouseX, float mouseY,
                            const aoc::ecs::World& world,
                            const aoc::map::HexGrid& grid,
                            const aoc::render::CameraController& camera,
                            const aoc::map::FogOfWar& fog,
                            PlayerId player,
                            uint32_t screenW, uint32_t screenH,
                            EntityId selectedEntity) {
    // Convert screen to world coordinates
    float worldX = 0.0f;
    float worldY = 0.0f;
    camera.screenToWorld(static_cast<double>(mouseX), static_cast<double>(mouseY),
                         worldX, worldY, screenW, screenH);

    constexpr float hexSize = 30.0f;  // Must match MapRenderer::m_hexSize
    aoc::hex::AxialCoord hovered = aoc::hex::pixelToAxial(worldX, worldY, hexSize);

    if (!grid.isValid(hovered)) {
        this->m_visible = false;
        this->m_showDelay = 0.0f;
        return;
    }

    // Accumulate delay before showing
    this->m_showDelay += 1.0f;
    if (this->m_showDelay < SHOW_DELAY_FRAMES) {
        this->m_visible = false;
        return;
    }

    const int32_t tileIndex = grid.toIndex(hovered);
    const aoc::map::TileVisibility vis = fog.visibility(player, tileIndex);

    if (vis == aoc::map::TileVisibility::Unseen) {
        this->m_visible = false;
        return;
    }

    // Build tooltip text
    std::string text;

    // Terrain
    const aoc::map::TerrainType terrain = grid.terrain(tileIndex);
    text += "Terrain: ";
    text += aoc::map::terrainName(terrain);

    // Feature
    const aoc::map::FeatureType feature = grid.feature(tileIndex);
    if (feature != aoc::map::FeatureType::None) {
        text += "\nFeature: ";
        text += aoc::map::featureName(feature);
    }

    // Improvement
    const aoc::map::ImprovementType improvement = grid.improvement(tileIndex);
    if (improvement != aoc::map::ImprovementType::None) {
        text += "\nImprovement: ";
        // Get name from index
        constexpr const char* IMPROVEMENT_NAMES[] = {
            "None", "Farm", "Mine", "Plantation", "Quarry",
            "Lumber Mill", "Camp", "Pasture", "Fishing Boats", "Fort", "Road"
        };
        const uint8_t impIdx = static_cast<uint8_t>(improvement);
        if (impIdx < 11) {
            text += IMPROVEMENT_NAMES[impIdx];
        }
    }

    // Yields
    const aoc::map::TileYield yields = grid.tileYield(tileIndex);
    text += "\nYields: F:";
    text += std::to_string(static_cast<int>(yields.food));
    text += " P:";
    text += std::to_string(static_cast<int>(yields.production));
    text += " G:";
    text += std::to_string(static_cast<int>(yields.gold));
    text += " S:";
    text += std::to_string(static_cast<int>(yields.science));

    // Owner
    const PlayerId tileOwner = grid.owner(tileIndex);
    if (tileOwner != INVALID_PLAYER) {
        text += "\nOwner: Player ";
        text += std::to_string(static_cast<unsigned>(tileOwner));
    } else {
        text += "\nOwner: Unowned";
    }

    // Resource
    // Resource (only if revealed by tech)
    const ResourceId resource = grid.resource(tileIndex);
    if (resource.isValid() && resource.value < aoc::sim::goods::GOOD_COUNT) {
        TechId revealTech = aoc::sim::resourceRevealTech(resource.value);
        bool resourceRevealed = true;
        if (revealTech.isValid()) {
            resourceRevealed = false;
            const aoc::ecs::ComponentPool<aoc::sim::PlayerTechComponent>* ttPool =
                world.getPool<aoc::sim::PlayerTechComponent>();
            if (ttPool != nullptr) {
                for (uint32_t ti = 0; ti < ttPool->size(); ++ti) {
                    if (ttPool->data()[ti].owner == player
                        && ttPool->data()[ti].hasResearched(revealTech)) {
                        resourceRevealed = true;
                        break;
                    }
                }
            }
        }
        if (resourceRevealed) {
            const aoc::sim::GoodDef& gdef = aoc::sim::goodDef(resource.value);
            text += "\nResource: ";
            text += gdef.name;
        }
    }

    // Check for units on this tile (own units always visible, enemy only if tile visible)
    {
        const aoc::ecs::ComponentPool<aoc::sim::UnitComponent>* unitPool =
            world.getPool<aoc::sim::UnitComponent>();
        if (unitPool != nullptr) {
            for (uint32_t i = 0; i < unitPool->size(); ++i) {
                const aoc::sim::UnitComponent& unit = unitPool->data()[i];
                // Show own units always, enemy units only on visible tiles
                if (unit.owner != player && vis != aoc::map::TileVisibility::Visible) {
                    continue;
                }
                if (unit.position == hovered) {
                    const aoc::sim::UnitTypeDef& udef = aoc::sim::unitTypeDef(unit.typeId);
                    text += "\nUnit: ";
                    text += udef.name;
                    text += " HP:";
                    text += std::to_string(unit.hitPoints);
                    text += " MP:";
                    text += std::to_string(unit.movementRemaining);

                    // Combat preview: if hovering an enemy unit and we have a unit selected
                    EntityId hoveredEntity = unitPool->entities()[i];
                    if (selectedEntity.isValid() && unit.owner != player &&
                        world.hasComponent<aoc::sim::UnitComponent>(selectedEntity)) {
                        const aoc::sim::UnitComponent& selUnit =
                            world.getComponent<aoc::sim::UnitComponent>(selectedEntity);
                        if (selUnit.owner == player) {
                            const aoc::sim::CombatPreview preview =
                                aoc::sim::previewCombat(world, grid, selectedEntity, hoveredEntity);
                            text += "\nAttack: ~";
                            text += std::to_string(preview.expectedDefenderDamage);
                            text += " damage to enemy, ~";
                            text += std::to_string(preview.expectedAttackerDamage);
                            text += " damage to you";
                        }
                    }
                }
            }
        }

        // Check for city on this tile (own cities always, foreign if revealed/visible)
        const aoc::ecs::ComponentPool<aoc::sim::CityComponent>* cityPool =
            world.getPool<aoc::sim::CityComponent>();
        if (cityPool != nullptr) {
            for (uint32_t i = 0; i < cityPool->size(); ++i) {
                const aoc::sim::CityComponent& city = cityPool->data()[i];
                if (city.owner != player && vis == aoc::map::TileVisibility::Unseen) {
                    continue;
                }
                if (city.location == hovered) {
                    const EntityId cityEntity = cityPool->entities()[i];

                    text += "\nCity: ";
                    text += city.name;
                    text += " Pop:";
                    text += std::to_string(city.population);

                    // Loyalty
                    const aoc::sim::CityLoyaltyComponent* loyaltyComp =
                        world.tryGetComponent<aoc::sim::CityLoyaltyComponent>(cityEntity);
                    if (loyaltyComp != nullptr) {
                        char loyaltyBuf[64];
                        std::snprintf(loyaltyBuf, sizeof(loyaltyBuf),
                            "\nLoyalty: %.0f/100 (%s)",
                            static_cast<double>(loyaltyComp->loyalty),
                            aoc::sim::loyaltyStatusName(loyaltyComp->status()));
                        text += loyaltyBuf;
                    }

                    // Happiness
                    const aoc::sim::CityHappinessComponent* happinessComp =
                        world.tryGetComponent<aoc::sim::CityHappinessComponent>(cityEntity);
                    if (happinessComp != nullptr) {
                        char happyBuf[64];
                        std::snprintf(happyBuf, sizeof(happyBuf),
                            "\nHappiness: %.0f amenities",
                            static_cast<double>(happinessComp->amenities));
                        text += happyBuf;
                    }

                    // Current production
                    const aoc::sim::ProductionQueueComponent* queueComp =
                        world.tryGetComponent<aoc::sim::ProductionQueueComponent>(cityEntity);
                    if (queueComp != nullptr) {
                        const aoc::sim::ProductionQueueItem* current = queueComp->currentItem();
                        if (current != nullptr) {
                            char prodBuf[128];
                            std::snprintf(prodBuf, sizeof(prodBuf),
                                "\nProduction: %s (%.0f/%.0f hammers)",
                                current->name.c_str(),
                                static_cast<double>(current->progress),
                                static_cast<double>(current->totalCost));
                            text += prodBuf;
                        }
                    }

                    // Gold income (sum gold from worked tiles)
                    int32_t cityGold = 0;
                    for (const hex::AxialCoord& tile : city.workedTiles) {
                        if (grid.isValid(tile)) {
                            const int32_t tileIdx = grid.toIndex(tile);
                            cityGold += grid.tileYield(tileIdx).gold;
                        }
                    }
                    text += "\nGold income: +";
                    text += std::to_string(cityGold);
                    text += "/turn";
                }
            }
        }
    }

    this->m_text = std::move(text);
    this->m_x = mouseX + TOOLTIP_OFFSET_X;
    this->m_y = mouseY + TOOLTIP_OFFSET_Y;

    // Clamp tooltip to screen bounds
    // Estimate tooltip dimensions
    std::size_t lineCount = 1;
    std::size_t maxLineLen = 0;
    std::size_t currentLineLen = 0;
    for (char ch : this->m_text) {
        if (ch == '\n') {
            ++lineCount;
            if (currentLineLen > maxLineLen) {
                maxLineLen = currentLineLen;
            }
            currentLineLen = 0;
        } else {
            ++currentLineLen;
        }
    }
    if (currentLineLen > maxLineLen) {
        maxLineLen = currentLineLen;
    }

    const float tooltipW = static_cast<float>(maxLineLen) * FONT_SIZE * 0.7f + PADDING * 2.0f;
    const float tooltipH = static_cast<float>(lineCount) * LINE_HEIGHT + PADDING * 2.0f;

    if (this->m_x + tooltipW > static_cast<float>(screenW)) {
        this->m_x = mouseX - tooltipW - 5.0f;
    }
    if (this->m_y + tooltipH > static_cast<float>(screenH)) {
        this->m_y = mouseY - tooltipH - 5.0f;
    }

    this->m_visible = true;
}

void TooltipManager::render(vulkan_app::renderer::Renderer2D& renderer2d) const {
    if (!this->m_visible || this->m_text.empty()) {
        return;
    }

    // Count lines and max line length for sizing
    std::size_t lineCount = 1;
    std::size_t maxLineLen = 0;
    std::size_t currentLineLen = 0;
    for (char ch : this->m_text) {
        if (ch == '\n') {
            ++lineCount;
            if (currentLineLen > maxLineLen) {
                maxLineLen = currentLineLen;
            }
            currentLineLen = 0;
        } else {
            ++currentLineLen;
        }
    }
    if (currentLineLen > maxLineLen) {
        maxLineLen = currentLineLen;
    }

    const float scale = this->m_renderScale;
    const float scaledFontSize = FONT_SIZE * scale;
    const float scaledLineHeight = LINE_HEIGHT * scale;
    const float scaledPadding = PADDING * scale;

    const float tooltipW = (static_cast<float>(maxLineLen) * FONT_SIZE * 0.7f + PADDING * 2.0f) * scale;
    const float tooltipH = (static_cast<float>(lineCount) * LINE_HEIGHT + PADDING * 2.0f) * scale;

    // Draw background panel
    renderer2d.drawRoundedRect(this->m_x, this->m_y, tooltipW, tooltipH,
                               3.0f * scale, 0.05f, 0.05f, 0.08f, 0.92f);

    // Thin accent border
    renderer2d.drawRoundedRect(this->m_x, this->m_y, tooltipW, tooltipH,
                               3.0f * scale, 0.3f, 0.3f, 0.4f, 0.5f);

    // Draw text lines
    const Color textColor{0.9f, 0.9f, 0.9f, 1.0f};
    float lineY = this->m_y + scaledPadding;
    std::size_t lineStart = 0;

    for (std::size_t i = 0; i <= this->m_text.size(); ++i) {
        if (i == this->m_text.size() || this->m_text[i] == '\n') {
            const std::string_view line(this->m_text.data() + lineStart, i - lineStart);
            if (!line.empty()) {
                BitmapFont::drawText(renderer2d, line,
                                     this->m_x + scaledPadding, lineY,
                                     scaledFontSize, textColor, scale);
            }
            lineY += scaledLineHeight;
            lineStart = i + 1;
        }
    }
}

} // namespace aoc::ui
