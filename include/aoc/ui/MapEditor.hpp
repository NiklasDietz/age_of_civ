#pragma once

/**
 * @file MapEditor.hpp
 * @brief Simple hex map editor accessible from the main menu.
 *
 * Allows painting terrain types, features, and resources onto a HexGrid.
 * Supports save/load via the existing Serializer.
 */

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/ui/Widget.hpp"
#include "aoc/core/Types.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace aoc::ui {
class UIManager;
}

namespace aoc::ui {

class MapEditor {
public:
    /// Build the editor toolbar UI.
    void build(UIManager& ui, float screenW, float screenH,
               std::function<void()> onBack,
               std::function<void(aoc::map::HexGrid&)> onPlayMap);

    /// Tear down all editor UI.
    void destroy(UIManager& ui);

    /// Handle a left click at the given hex tile index (cycle terrain).
    void handleLeftClick(aoc::map::HexGrid& grid, int32_t tileIndex);

    /// Handle a right click at the given hex tile index (cycle feature).
    void handleRightClick(aoc::map::HexGrid& grid, int32_t tileIndex);

    /// Save the current map to a file.
    void saveMap(const aoc::map::HexGrid& grid, const std::string& filepath) const;

    /// Load a map from a file.
    bool loadMap(aoc::map::HexGrid& grid, const std::string& filepath) const;

    [[nodiscard]] bool isBuilt() const { return this->m_isBuilt; }

    /// Currently selected terrain type for painting.
    aoc::map::TerrainType selectedTerrain = aoc::map::TerrainType::Grassland;

    /// Currently selected feature type for painting.
    aoc::map::FeatureType selectedFeature = aoc::map::FeatureType::None;

private:
    bool m_isBuilt = false;
    WidgetId m_rootPanel = INVALID_WIDGET;
};

} // namespace aoc::ui
