#pragma once

/**
 * @file TradeRouteSetupScreen.hpp
 * @brief Modal screen for assigning Trader units to destination cities.
 *
 * Three-step workflow:
 *   1. Select an idle Trader unit from the player's unit list.
 *   2. Select a destination city (own or foreign with trade access).
 *   3. Preview estimated income/distance and confirm to establish the route.
 *
 * Calls `establishTradeRoute()` from TradeRouteSystem on confirmation.
 */

#include "aoc/ui/GameScreens.hpp"
#include "aoc/core/Types.hpp"

#include <cstdint>
#include <vector>

namespace aoc::game { class GameState; class Unit; class City; }
namespace aoc::map  { class HexGrid; }
namespace aoc::sim  { class Market; class DiplomacyManager; }

namespace aoc::ui {

class TradeRouteSetupScreen final : public ScreenBase {
public:
    void setContext(aoc::game::GameState* gameState, aoc::map::HexGrid* grid,
                    PlayerId humanPlayer, const aoc::sim::Market* market,
                    aoc::sim::DiplomacyManager* diplomacy);

    void open(UIManager& ui) override;
    void close(UIManager& ui) override;
    void refresh(UIManager& ui) override;

private:
    /// Rebuild the destination list and preview after selecting a Trader.
    void buildDestinationPanel(UIManager& ui, WidgetId innerPanel);

    /// Show the route preview (distance, est. income, route type) and confirm button.
    void buildRoutePreview(UIManager& ui, WidgetId innerPanel);

    aoc::game::GameState*        m_gameState = nullptr;
    aoc::map::HexGrid*           m_grid      = nullptr;
    const aoc::sim::Market*      m_market    = nullptr;
    aoc::sim::DiplomacyManager*  m_diplomacy = nullptr;
    PlayerId                     m_player    = INVALID_PLAYER;

    /// Currently selected Trader unit (raw pointer into player's unit list).
    aoc::game::Unit*             m_selectedTrader = nullptr;

    /// Currently selected destination city.
    aoc::game::City*             m_selectedDest   = nullptr;

    /// Widgets that get rebuilt on selection changes.
    WidgetId m_statusLabel    = INVALID_WIDGET;
    WidgetId m_traderList     = INVALID_WIDGET;
    WidgetId m_destPanel      = INVALID_WIDGET;
    WidgetId m_previewPanel   = INVALID_WIDGET;
};

} // namespace aoc::ui
