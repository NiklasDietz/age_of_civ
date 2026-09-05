#pragma once

/**
 * @file WorldCongressScreen.hpp
 * @brief World Congress screen: session status, the current proposal with its
 *        votes and buttons to change the player's own vote, the proposal the
 *        player registered for when it is chosen as proposer, the player's favor,
 *        active effects and passed resolutions. Every button goes through the
 *        same validated request as the debug routes and the MCP tools.
 */

#include "aoc/ui/GameScreens.hpp"
#include "aoc/core/Types.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace aoc::map {
class HexGrid;
}
namespace aoc::game {
class GameState;
}

namespace aoc::ui {

class WorldCongressScreen final : public ScreenBase {
public:
    /// Pointers are non-owning and must outlive the open screen.
    void setContext(aoc::game::GameState* gameState, const aoc::map::HexGrid* grid,
                    PlayerId humanPlayer);

    void open(UIManager& ui) override;
    void close(UIManager& ui) override;
    void refresh(UIManager& ui) override;

private:
    void buildRows(UIManager& ui);
    void addProposalRows(UIManager& ui);
    void addVoteButtons(UIManager& ui, int32_t currentWeight);
    void addPreferenceRows(UIManager& ui);
    void addEffectRows(UIManager& ui);
    void addHeader(UIManager& ui, const std::string& text);
    void addLine(UIManager& ui, std::string text, bool dim);
    /// A wrap-layout panel in the list, sized for `buttons` buttons of `buttonWidth`.
    [[nodiscard]] WidgetId addButtonRow(UIManager& ui, int32_t buttons, float buttonWidth);
    void addActionButton(UIManager& ui, WidgetId row, float width, std::string label,
                         std::function<void()> onClick);
    [[nodiscard]] std::string civLabel(PlayerId id) const;

    /// Cheap change detector so `refresh` rebuilds only when the congress moved.
    [[nodiscard]] uint64_t stateFingerprint() const;

    aoc::game::GameState* m_gameState = nullptr;
    const aoc::map::HexGrid* m_grid   = nullptr;
    PlayerId m_player                 = INVALID_PLAYER;
    WidgetId m_summaryLabel           = INVALID_WIDGET;
    WidgetId m_list                   = INVALID_WIDGET;
    uint64_t m_shownFingerprint       = 0;
};

} // namespace aoc::ui
