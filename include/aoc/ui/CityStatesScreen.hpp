/**
 * @file CityStatesScreen.hpp
 * @brief Met city-states with envoys, suzerain, quest and levy, plus the envoy pool.
 *
 * Reached from the Menu dropdown. Send Envoy / Levy / Bully go through the
 * request functions in CityState.hpp, the same ones the debug routes and the
 * MCP tools use. Rows are rebuilt when the state fingerprint changes.
 */

#pragma once

#include "aoc/ui/GameScreens.hpp"
#include "aoc/core/Types.hpp"

#include <cstdint>
#include <string>

namespace aoc::map { class HexGrid; }
namespace aoc::game { class GameState; }

namespace aoc::ui {

class CityStatesScreen final : public ScreenBase {
public:
    void setContext(aoc::game::GameState* gameState, const aoc::map::HexGrid* grid,
                    PlayerId humanPlayer);

    void open(UIManager& ui) override;
    void close(UIManager& ui) override;
    void refresh(UIManager& ui) override;

private:
    void buildRows(UIManager& ui);
    void addHeader(UIManager& ui, const char* text);
    void addLine(UIManager& ui, std::string text, bool dim);
    void addActionRow(UIManager& ui, std::size_t cityStateIndex);
    [[nodiscard]] uint64_t stateFingerprint() const;

    aoc::game::GameState* m_gameState = nullptr;
    const aoc::map::HexGrid* m_grid   = nullptr;
    PlayerId m_player                 = INVALID_PLAYER;
    WidgetId m_summaryLabel           = INVALID_WIDGET;
    WidgetId m_list                   = INVALID_WIDGET;
    uint64_t m_shownFingerprint       = 0;
};

} // namespace aoc::ui
