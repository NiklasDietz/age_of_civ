#pragma once

/**
 * @file GameScreens.hpp
 * @brief Modal game screens for player interaction.
 *
 * Each screen is a class that builds/destroys widgets in UIManager.
 * Screens overlay the game view and block game input while open.
 */

#include "aoc/ui/Widget.hpp"
#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"

#include <array>
#include <string>

namespace aoc::ui { class UIManager; }
namespace aoc::ecs { class World; }
namespace aoc::map { class HexGrid; }
namespace aoc::sim { class Market; }

namespace aoc::ui {

/// Manages a single modal screen overlay.
class ScreenBase {
public:
    virtual ~ScreenBase() = default;

    [[nodiscard]] bool isOpen() const { return this->m_isOpen; }

    /// Toggle the screen open/closed.
    void toggle(UIManager& ui);

    /// Update the stored screen dimensions used when building the screen UI.
    void setScreenSize(float width, float height) {
        this->m_screenW = width;
        this->m_screenH = height;
    }

    /// Build and show the screen UI.
    virtual void open(UIManager& ui) = 0;

    /// Tear down the screen UI.
    virtual void close(UIManager& ui) = 0;

    /// Refresh dynamic content (labels, lists) while the screen is open.
    virtual void refresh(UIManager& ui) = 0;

protected:
    WidgetId m_rootPanel = INVALID_WIDGET;
    bool m_isOpen = false;
    float m_screenW = 1280.0f;
    float m_screenH = 720.0f;

    /// Helper to create the standard screen frame (dark overlay + centered panel).
    /// Returns the inner panel WidgetId to which screens add their content.
    WidgetId createScreenFrame(UIManager& ui, const std::string& title,
                                float width, float height,
                                float screenW, float screenH);
};

/// City production picker screen.
class ProductionScreen final : public ScreenBase {
public:
    void setContext(aoc::ecs::World* world, aoc::map::HexGrid* grid,
                    EntityId cityEntity, PlayerId player);
    void open(UIManager& ui) override;
    void close(UIManager& ui) override;
    void refresh(UIManager& ui) override;

private:
    aoc::ecs::World* m_world = nullptr;
    aoc::map::HexGrid* m_grid = nullptr;
    EntityId m_cityEntity = NULL_ENTITY;
    PlayerId m_player = INVALID_PLAYER;
    WidgetId m_queueLabel = INVALID_WIDGET;
    WidgetId m_itemList = INVALID_WIDGET;
};

/// Technology research screen.
class TechScreen final : public ScreenBase {
public:
    void setContext(aoc::ecs::World* world, PlayerId player);
    void open(UIManager& ui) override;
    void close(UIManager& ui) override;
    void refresh(UIManager& ui) override;

private:
    aoc::ecs::World* m_world = nullptr;
    PlayerId m_player = INVALID_PLAYER;
    WidgetId m_currentLabel = INVALID_WIDGET;
    WidgetId m_techList = INVALID_WIDGET;
};

/// Government and policy management screen.
class GovernmentScreen final : public ScreenBase {
public:
    void setContext(aoc::ecs::World* world, PlayerId player);
    void open(UIManager& ui) override;
    void close(UIManager& ui) override;
    void refresh(UIManager& ui) override;

private:
    aoc::ecs::World* m_world = nullptr;
    PlayerId m_player = INVALID_PLAYER;
    WidgetId m_currentGovLabel = INVALID_WIDGET;
    WidgetId m_govList = INVALID_WIDGET;
};

/// Economy overview and market screen.
class EconomyScreen final : public ScreenBase {
public:
    void setContext(aoc::ecs::World* world, const aoc::map::HexGrid* grid,
                    PlayerId player, const aoc::sim::Market* market = nullptr);
    void open(UIManager& ui) override;
    void close(UIManager& ui) override;
    void refresh(UIManager& ui) override;

private:
    /// Build the trade route creation sub-panel.
    void buildTradeRoutePanel(UIManager& ui, WidgetId parentPanel);

    aoc::ecs::World* m_world = nullptr;
    const aoc::map::HexGrid* m_grid = nullptr;
    const aoc::sim::Market* m_market = nullptr;
    PlayerId m_player = INVALID_PLAYER;
    WidgetId m_infoLabel = INVALID_WIDGET;
    WidgetId m_marketList = INVALID_WIDGET;
    WidgetId m_tradeRoutePanel = INVALID_WIDGET;
    EntityId m_trSourceCity = NULL_ENTITY;
    EntityId m_trDestCity = NULL_ENTITY;
};

/// Detailed city information screen (right-side panel, does not block map input).
class CityDetailScreen final : public ScreenBase {
public:
    void setContext(aoc::ecs::World* world, const aoc::map::HexGrid* grid,
                    EntityId cityEntity, PlayerId player);
    void open(UIManager& ui) override;
    void close(UIManager& ui) override;
    void refresh(UIManager& ui) override;

    /// Switch to a different tab (rebuilds the content area).
    void switchTab(UIManager& ui, int32_t tabIndex);

    /// Returns true because this screen is a right-side panel that does not block map input.
    [[nodiscard]] bool isRightSidePanel() const { return true; }

    /// Toggle worker assignment for a tile. Called from Application when
    /// the player left-clicks a tile while the city detail screen is open.
    void toggleWorkerOnTile(aoc::hex::AxialCoord tile);

    /// The city entity currently displayed by this screen.
    [[nodiscard]] EntityId cityEntity() const { return this->m_cityEntity; }

    /// Current active tab index.
    static constexpr int32_t TAB_OVERVIEW   = 0;
    static constexpr int32_t TAB_PRODUCTION = 1;
    static constexpr int32_t TAB_BUILDINGS  = 2;
    static constexpr int32_t TAB_CITIZENS   = 3;
    static constexpr int32_t TAB_COUNT      = 4;

private:
    void buildOverviewTab(UIManager& ui, WidgetId contentPanel);
    void buildProductionTab(UIManager& ui, WidgetId contentPanel);
    void buildBuildingsTab(UIManager& ui, WidgetId contentPanel);
    void buildCitizensTab(UIManager& ui, WidgetId contentPanel);

    /// Update tab button colors to reflect the active tab.
    void updateTabButtonColors(UIManager& ui);

    aoc::ecs::World* m_world = nullptr;
    const aoc::map::HexGrid* m_grid = nullptr;
    EntityId m_cityEntity = NULL_ENTITY;
    PlayerId m_player = INVALID_PLAYER;
    WidgetId m_detailLabel = INVALID_WIDGET;
    WidgetId m_contentPanel = INVALID_WIDGET;  ///< Panel that holds the active tab's content
    std::array<WidgetId, TAB_COUNT> m_tabButtons = {
        INVALID_WIDGET, INVALID_WIDGET, INVALID_WIDGET, INVALID_WIDGET};
    int32_t m_activeTab = TAB_OVERVIEW;
};

} // namespace aoc::ui
