#pragma once

/**
 * @file ReligionScreen.hpp
 * @brief Modal religion screen for managing faith, founding religions,
 *        and purchasing religious units.
 */

#include "aoc/ui/GameScreens.hpp"
#include "aoc/core/Types.hpp"

#include <cstdint>

namespace aoc::map { class HexGrid; }
namespace aoc::sim { using ReligionId = uint8_t; }

namespace aoc::game {
class GameState;
}

namespace aoc::ui {

class ReligionScreen final : public ScreenBase {
public:
    void setContext(aoc::game::GameState* gameState, aoc::map::HexGrid* grid, PlayerId humanPlayer);

    void open(UIManager& ui) override;
    void close(UIManager& ui) override;
    void refresh(UIManager& ui) override;

private:
    /// Build the belief/action list content inside the scroll list.
    void buildBeliefList(UIManager& ui);

    /// Spawn a religious unit at the player's holy city.
    void spawnReligiousUnit(UnitTypeId typeId, aoc::sim::ReligionId religion);

    aoc::game::GameState* m_gameState = nullptr;
    aoc::map::HexGrid*    m_grid      = nullptr;
    PlayerId              m_player    = INVALID_PLAYER;
    WidgetId              m_faithLabel   = INVALID_WIDGET;
    WidgetId              m_statusLabel  = INVALID_WIDGET;
    WidgetId              m_beliefList   = INVALID_WIDGET;
};

} // namespace aoc::ui
