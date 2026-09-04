#pragma once

/**
 * @file GreatPeopleScreen.hpp
 * @brief Read-only Great People screen: progress toward each type, where the
 *        points come from, and the recruited people waiting to be used.
 */

#include "aoc/ui/GameScreens.hpp"
#include "aoc/core/Types.hpp"

#include <cstdint>
#include <string>

namespace aoc::map { class HexGrid; }
namespace aoc::game {
class GameState;
class Player;
}

namespace aoc::ui {

class GreatPeopleScreen final : public ScreenBase {
public:
    /// All pointers are non-owning and must outlive the open screen.
    void setContext(aoc::game::GameState* gameState, const aoc::map::HexGrid* grid,
                    PlayerId humanPlayer);

    void open(UIManager& ui) override;
    void close(UIManager& ui) override;
    void refresh(UIManager& ui) override;

private:
    void buildRows(UIManager& ui);
    void addProgressRows(UIManager& ui, const aoc::game::Player& player);
    void addRecruitedRows(UIManager& ui, const aoc::game::Player& player);
    void addHeader(UIManager& ui, const std::string& text);
    void addLine(UIManager& ui, std::string text, bool dim);

    /// Cheap change detector so `refresh` rebuilds only when the state moved.
    [[nodiscard]] uint64_t stateFingerprint() const;

    aoc::game::GameState*    m_gameState        = nullptr;
    const aoc::map::HexGrid* m_grid             = nullptr;
    PlayerId                 m_player           = INVALID_PLAYER;
    WidgetId                 m_summaryLabel     = INVALID_WIDGET;
    WidgetId                 m_list             = INVALID_WIDGET;
    uint64_t                 m_shownFingerprint = 0;
};

} // namespace aoc::ui
