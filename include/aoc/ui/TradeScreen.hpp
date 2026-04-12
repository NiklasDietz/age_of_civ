#pragma once

/**
 * @file TradeScreen.hpp
 * @brief Modal trade deal screen for proposing trades between players.
 *
 * Allows the human player to select a trade partner, adjust offered/requested
 * goods and gold amounts, and propose the deal for AI evaluation.
 */

#include "aoc/ui/GameScreens.hpp"
#include "aoc/core/Types.hpp"

#include <cstdint>

namespace aoc::sim {
class Market;
class DiplomacyManager;
}

namespace aoc::game {
class GameState;
}

namespace aoc::ui {

class TradeScreen final : public ScreenBase {
public:
    void setContext(aoc::game::GameState* gameState, PlayerId humanPlayer,
                    const aoc::sim::Market* market,
                    aoc::sim::DiplomacyManager* diplomacy);

    void open(UIManager& ui) override;
    void close(UIManager& ui) override;
    void refresh(UIManager& ui) override;

private:
    /// Build the trade columns for a selected partner.
    void buildTradeColumns(UIManager& ui, WidgetId innerPanel, PlayerId partner);

    aoc::game::GameState*        m_gameState = nullptr;
    const aoc::sim::Market*      m_market    = nullptr;
    aoc::sim::DiplomacyManager*  m_diplomacy = nullptr;
    PlayerId                     m_player    = INVALID_PLAYER;
    PlayerId                     m_partner   = INVALID_PLAYER;
    WidgetId                     m_statusLabel = INVALID_WIDGET;
    WidgetId                     m_partnerList = INVALID_WIDGET;
    WidgetId                     m_tradePanel  = INVALID_WIDGET;
    /// Amounts the human offers / requests (indexed by good ID).
    static constexpr uint16_t MAX_TRADE_GOODS = 32;
    int32_t m_offerAmounts[MAX_TRADE_GOODS]   = {};
    int32_t m_requestAmounts[MAX_TRADE_GOODS] = {};
    int32_t m_offerGold   = 0;
    int32_t m_requestGold = 0;
};

} // namespace aoc::ui
