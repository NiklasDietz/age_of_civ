/**
 * @file MapEditor.cpp
 * @brief Simple hex map editor implementation.
 */

#include "aoc/ui/MapEditor.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/StyleTokens.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/core/Log.hpp"

#include <fstream>

namespace aoc::ui {

void MapEditor::build(UIManager& ui, float screenW, float /*screenH*/,
                       std::function<void()> onBack,
                       std::function<void(aoc::map::HexGrid&)> /*onPlayMap*/) {
    if (this->m_isBuilt) {
        return;
    }

    // Toolbar panel at the top of the screen
    constexpr float TOOLBAR_H = 40.0f;
    this->m_rootPanel = ui.createPanel(
        {0.0f, 0.0f, screenW, TOOLBAR_H},
        PanelData{tokens::SURFACE_INK, 0.0f});

    Widget* rootWidget = ui.getWidget(this->m_rootPanel);
    if (rootWidget != nullptr) {
        rootWidget->layoutDirection = LayoutDirection::Horizontal;
        rootWidget->padding = {4.0f, 8.0f, 4.0f, 8.0f};
        rootWidget->childSpacing = 6.0f;
    }

    // Back button
    {
        ButtonData btn;
        btn.label = "Back";
        btn.fontSize = 12.0f;
        btn.normalColor  = tokens::STATE_DANGER;
        btn.hoverColor   = tokens::DIPLO_HOSTILE;
        btn.pressedColor = tokens::DIPLO_AT_WAR;
        btn.cornerRadius = 3.0f;
        btn.onClick = std::move(onBack);
        (void)ui.createButton(this->m_rootPanel, {0.0f, 0.0f, 60.0f, 28.0f}, std::move(btn));
    }

    // Terrain label
    (void)ui.createLabel(this->m_rootPanel, {0.0f, 0.0f, 120.0f, 28.0f},
                          LabelData{"LClick: Terrain  RClick: Feature",
                                    tokens::TEXT_PARCHMENT, 10.0f});

    // Save button
    {
        ButtonData btn;
        btn.label = "Save Map";
        btn.fontSize = 11.0f;
        btn.normalColor  = tokens::STATE_SUCCESS;
        btn.hoverColor   = tokens::DIPLO_FRIENDLY;
        btn.pressedColor = tokens::STATE_PRESSED;
        btn.cornerRadius = 3.0f;
        btn.onClick = []() {
            LOG_INFO("Map save requested (use saveMap method)");
        };
        (void)ui.createButton(this->m_rootPanel, {0.0f, 0.0f, 80.0f, 28.0f}, std::move(btn));
    }

    // Load button
    {
        ButtonData btn;
        btn.label = "Load Map";
        btn.fontSize = 11.0f;
        btn.normalColor  = tokens::DIPLO_ALLIED;
        btn.hoverColor   = tokens::RES_SCIENCE;
        btn.pressedColor = tokens::SURFACE_INK;
        btn.cornerRadius = 3.0f;
        btn.onClick = []() {
            LOG_INFO("Map load requested (use loadMap method)");
        };
        (void)ui.createButton(this->m_rootPanel, {0.0f, 0.0f, 80.0f, 28.0f}, std::move(btn));
    }

    this->m_isBuilt = true;
    LOG_INFO("Map editor built");
}

void MapEditor::destroy(UIManager& ui) {
    if (!this->m_isBuilt) {
        return;
    }
    if (this->m_rootPanel != INVALID_WIDGET) {
        ui.removeWidget(this->m_rootPanel);
        this->m_rootPanel = INVALID_WIDGET;
    }
    this->m_isBuilt = false;
}

void MapEditor::handleLeftClick(aoc::map::HexGrid& grid, int32_t tileIndex) {
    if (tileIndex < 0 || tileIndex >= grid.tileCount()) {
        return;
    }

    // Cycle terrain type
    const uint8_t current = static_cast<uint8_t>(grid.terrain(tileIndex));
    const uint8_t next = (current + 1) % static_cast<uint8_t>(aoc::map::TerrainType::Count);
    grid.setTerrain(tileIndex, static_cast<aoc::map::TerrainType>(next));
}

void MapEditor::handleRightClick(aoc::map::HexGrid& grid, int32_t tileIndex) {
    if (tileIndex < 0 || tileIndex >= grid.tileCount()) {
        return;
    }

    // Cycle feature type
    const uint8_t current = static_cast<uint8_t>(grid.feature(tileIndex));
    const uint8_t next = (current + 1) % static_cast<uint8_t>(aoc::map::FeatureType::Count);
    grid.setFeature(tileIndex, static_cast<aoc::map::FeatureType>(next));
}

void MapEditor::saveMap(const aoc::map::HexGrid& grid,
                         const std::string& filepath) const {
    std::ofstream file(filepath, std::ios::binary);
    if (!file) {
        LOG_ERROR("Failed to save map to %s", filepath.c_str());
        return;
    }

    const int32_t w = grid.width();
    const int32_t h = grid.height();
    file.write(reinterpret_cast<const char*>(&w), sizeof(w));
    file.write(reinterpret_cast<const char*>(&h), sizeof(h));

    for (int32_t i = 0; i < grid.tileCount(); ++i) {
        const uint8_t terrain = static_cast<uint8_t>(grid.terrain(i));
        const uint8_t feature = static_cast<uint8_t>(grid.feature(i));
        file.write(reinterpret_cast<const char*>(&terrain), 1);
        file.write(reinterpret_cast<const char*>(&feature), 1);
    }

    LOG_INFO("Map saved to %s (%dx%d)", filepath.c_str(), w, h);
}

bool MapEditor::loadMap(aoc::map::HexGrid& grid,
                         const std::string& filepath) const {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        LOG_ERROR("Failed to load map from %s", filepath.c_str());
        return false;
    }

    int32_t w = 0, h = 0;
    file.read(reinterpret_cast<char*>(&w), sizeof(w));
    file.read(reinterpret_cast<char*>(&h), sizeof(h));

    if (w <= 0 || h <= 0 || w > 200 || h > 200) {
        LOG_ERROR("Invalid map dimensions %dx%d in %s", w, h, filepath.c_str());
        return false;
    }

    grid.initialize(w, h);

    for (int32_t i = 0; i < grid.tileCount(); ++i) {
        uint8_t terrain = 0;
        uint8_t feature = 0;
        file.read(reinterpret_cast<char*>(&terrain), 1);
        file.read(reinterpret_cast<char*>(&feature), 1);
        grid.setTerrain(i, static_cast<aoc::map::TerrainType>(terrain));
        grid.setFeature(i, static_cast<aoc::map::FeatureType>(feature));
    }

    LOG_INFO("Map loaded from %s (%dx%d)", filepath.c_str(), w, h);
    return true;
}

} // namespace aoc::ui
