/**
 * @file GreatWorksScreen.hpp
 * @brief Great Works by city and slot, with move buttons, plus the tourism race.
 *
 * Reached from the Menu dropdown. Moves go through requestMoveGreatWork, the
 * same request the debug route and the MCP tool use. Rows are rebuilt when the
 * state fingerprint changes (a work placed or moved, a slot built, a new turn).
 */

#pragma once

#include "aoc/ui/GameScreens.hpp"
#include "aoc/core/Types.hpp"

#include <cstdint>
#include <string>

namespace aoc::map { class HexGrid; }
namespace aoc::game { class GameState; }
namespace aoc::sim { class DiplomacyManager; }

namespace aoc::ui {

class GreatWorksScreen final : public ScreenBase {
public:
    /// `diplomacy` may be null; then every living rival is listed.
    void setContext(aoc::game::GameState* gameState, const aoc::map::HexGrid* grid,
                    PlayerId humanPlayer, const aoc::sim::DiplomacyManager* diplomacy = nullptr);

    void open(UIManager& ui) override;
    void close(UIManager& ui) override;
    void refresh(UIManager& ui) override;

private:
    void buildRows(UIManager& ui);
    void addTourismRows(UIManager& ui);
    void addCityRows(UIManager& ui);
    void addHeader(UIManager& ui, const char* text);
    void addLine(UIManager& ui, std::string text, bool dim);
    [[nodiscard]] uint64_t stateFingerprint() const;

    aoc::game::GameState* m_gameState              = nullptr;
    const aoc::map::HexGrid* m_grid                = nullptr;
    const aoc::sim::DiplomacyManager* m_diplomacy  = nullptr;
    PlayerId m_player                              = INVALID_PLAYER;
    WidgetId m_summaryLabel                        = INVALID_WIDGET;
    WidgetId m_list                                = INVALID_WIDGET;
    uint64_t m_shownFingerprint                    = 0;
};

} // namespace aoc::ui
