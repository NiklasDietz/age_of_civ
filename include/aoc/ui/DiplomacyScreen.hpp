#pragma once

/**
 * @file DiplomacyScreen.hpp
 * @brief Modal diplomacy screen showing relations and diplomatic actions.
 *
 * Displays all known players with their relation scores, stances, war status,
 * and provides buttons for declaring war, proposing peace, and open borders.
 */

#include "aoc/ui/GameScreens.hpp"
#include "aoc/core/Types.hpp"

namespace aoc::sim {
class DiplomacyManager;
struct GlobalDealTracker;
}

namespace aoc::game {
class GameState;
}

namespace aoc::map {
class HexGrid;
}

namespace aoc::ui {

class DiplomacyScreen final : public ScreenBase {
public:
    void setContext(aoc::game::GameState* gameState, PlayerId humanPlayer,
                    aoc::sim::DiplomacyManager* diplomacy,
                    aoc::map::HexGrid* grid = nullptr,
                    aoc::sim::GlobalDealTracker* dealTracker = nullptr);

    void open(UIManager& ui) override;
    void close(UIManager& ui) override;
    void refresh(UIManager& ui) override;

private:
    aoc::game::GameState*          m_gameState   = nullptr;
    aoc::sim::DiplomacyManager*    m_diplomacy   = nullptr;
    aoc::map::HexGrid*             m_grid        = nullptr;
    aoc::sim::GlobalDealTracker*   m_dealTracker = nullptr;
    PlayerId                       m_player      = INVALID_PLAYER;
    WidgetId                       m_playerList  = INVALID_WIDGET;
};

} // namespace aoc::ui
