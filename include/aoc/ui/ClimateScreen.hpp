/**
 * @file ClimateScreen.hpp
 * @brief Global climate readout: temperature, CO2, sea level, thresholds and recent disasters.
 *
 * Read-only, reached from the Menu dropdown. Rows are rebuilt when the climate
 * numbers or the disaster history change.
 */

#pragma once

#include "aoc/ui/GameScreens.hpp"
#include "aoc/core/Types.hpp"

#include <cstdint>
#include <string>

namespace aoc::map { class HexGrid; }
namespace aoc::game { class GameState; }

namespace aoc::ui {

class ClimateScreen final : public ScreenBase {
public:
    void setContext(aoc::game::GameState* gameState, const aoc::map::HexGrid* grid, PlayerId humanPlayer);

    void open(UIManager& ui) override;
    void close(UIManager& ui) override;
    void refresh(UIManager& ui) override;

private:
    void buildRows(UIManager& ui);
    void addHeader(UIManager& ui, const char* text);
    void addLine(UIManager& ui, std::string text, bool dim);
    [[nodiscard]] uint64_t stateFingerprint() const;

    aoc::game::GameState* m_gameState = nullptr;
    const aoc::map::HexGrid* m_grid   = nullptr;
    PlayerId m_player                 = INVALID_PLAYER;
    WidgetId m_summaryLabel           = INVALID_WIDGET;
    WidgetId m_list                   = INVALID_WIDGET;
    uint64_t m_shownFingerprint       = 0;
};

} // namespace aoc::ui
