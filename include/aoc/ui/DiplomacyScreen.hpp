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
#include "aoc/simulation/diplomacy/DealTerms.hpp"

#include <vector>

namespace aoc::sim {
class DiplomacyManager;
struct GlobalDealTracker;
struct AllianceObligationTracker;
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
                    aoc::sim::GlobalDealTracker* dealTracker = nullptr,
                    aoc::sim::AllianceObligationTracker* obligations = nullptr);

    void open(UIManager& ui) override;
    void close(UIManager& ui) override;
    void refresh(UIManager& ui) override;

private:
    aoc::game::GameState*          m_gameState   = nullptr;
    aoc::sim::DiplomacyManager*    m_diplomacy   = nullptr;
    aoc::map::HexGrid*             m_grid        = nullptr;
    aoc::sim::GlobalDealTracker*   m_dealTracker = nullptr;
    aoc::sim::AllianceObligationTracker* m_obligations = nullptr;
    /// Civ whose casus belli picker is open (Declare War is a two-step choice).
    PlayerId                       m_warTarget   = INVALID_PLAYER;
    /// Civ whose deal composer is open, and the terms toggled so far.
    PlayerId                       m_composerTarget = INVALID_PLAYER;
    std::vector<aoc::sim::DealTerm> m_composerTerms;
    void toggleComposerTerm(aoc::sim::DealTermType type, PlayerId from, PlayerId to, int32_t gold);
    PlayerId                       m_player      = INVALID_PLAYER;
    WidgetId                       m_playerList  = INVALID_WIDGET;
};

} // namespace aoc::ui
