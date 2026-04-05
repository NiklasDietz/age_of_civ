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

#include <string>

namespace aoc::ui { class UIManager; }
namespace aoc::ecs { class World; }
namespace aoc::map { class HexGrid; }

namespace aoc::ui {

/// Manages a single modal screen overlay.
class ScreenBase {
public:
    virtual ~ScreenBase() = default;

    [[nodiscard]] bool isOpen() const { return this->m_isOpen; }

    /// Toggle the screen open/closed.
    void toggle(UIManager& ui);

    /// Build and show the screen UI.
    virtual void open(UIManager& ui) = 0;

    /// Tear down the screen UI.
    virtual void close(UIManager& ui) = 0;

    /// Refresh dynamic content (labels, lists) while the screen is open.
    virtual void refresh(UIManager& ui) = 0;

protected:
    WidgetId m_rootPanel = INVALID_WIDGET;
    bool m_isOpen = false;

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
    float m_screenW = 1280.0f;
    float m_screenH = 720.0f;
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
    float m_screenW = 1280.0f;
    float m_screenH = 720.0f;
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
    float m_screenW = 1280.0f;
    float m_screenH = 720.0f;
};

/// Economy overview and market screen.
class EconomyScreen final : public ScreenBase {
public:
    void setContext(aoc::ecs::World* world, const aoc::map::HexGrid* grid, PlayerId player);
    void open(UIManager& ui) override;
    void close(UIManager& ui) override;
    void refresh(UIManager& ui) override;

private:
    aoc::ecs::World* m_world = nullptr;
    const aoc::map::HexGrid* m_grid = nullptr;
    PlayerId m_player = INVALID_PLAYER;
    WidgetId m_infoLabel = INVALID_WIDGET;
    WidgetId m_marketList = INVALID_WIDGET;
    float m_screenW = 1280.0f;
    float m_screenH = 720.0f;
};

/// Detailed city information screen.
class CityDetailScreen final : public ScreenBase {
public:
    void setContext(aoc::ecs::World* world, const aoc::map::HexGrid* grid,
                    EntityId cityEntity, PlayerId player);
    void open(UIManager& ui) override;
    void close(UIManager& ui) override;
    void refresh(UIManager& ui) override;

private:
    aoc::ecs::World* m_world = nullptr;
    const aoc::map::HexGrid* m_grid = nullptr;
    EntityId m_cityEntity = NULL_ENTITY;
    PlayerId m_player = INVALID_PLAYER;
    WidgetId m_detailLabel = INVALID_WIDGET;
    float m_screenW = 1280.0f;
    float m_screenH = 720.0f;
};

} // namespace aoc::ui
