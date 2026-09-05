#pragma once

/**
 * @file DemographicsScreen.hpp
 * @brief Read-only Demographics screen: the human's rank on a few cheap
 *        metrics against every civilization it has met, Civ-style.
 */

#include "aoc/ui/GameScreens.hpp"
#include "aoc/core/Types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace aoc::map { class HexGrid; }
namespace aoc::game {
class GameState;
class Player;
}
namespace aoc::sim { class DiplomacyManager; }

namespace aoc::ui {

class DemographicsScreen final : public ScreenBase {
public:
    /// All pointers are non-owning and must outlive the open screen. Only civs
    /// the human has met (per `diplomacy`) are ranked; a null manager ranks all.
    void setContext(aoc::game::GameState* gameState, const aoc::map::HexGrid* grid,
                    PlayerId humanPlayer, const aoc::sim::DiplomacyManager* diplomacy);

    void open(UIManager& ui) override;
    void close(UIManager& ui) override;
    void refresh(UIManager& ui) override;

private:
    /// One civilization's numbers for this screen.
    struct Row {
        PlayerId id = INVALID_PLAYER;
        int64_t  population = 0;
        int64_t  cities     = 0;
        int64_t  military   = 0;
        int64_t  techs      = 0;
        int64_t  treasury   = 0;
        int64_t  gdp        = 0;
        int64_t  tourism    = 0;
        int64_t  score      = 0;
    };

    [[nodiscard]] std::vector<Row> knownRows(int32_t& unmetOut) const;
    void buildRows(UIManager& ui);
    void addMetricRow(UIManager& ui, const std::vector<Row>& rows, const char* label,
                      int64_t Row::*field);
    void addCivRows(UIManager& ui, std::vector<Row> rows);
    void addHeader(UIManager& ui, const std::string& text);
    void addLine(UIManager& ui, std::string text, bool dim);

    /// Cheap change detector so `refresh` rebuilds only when the numbers moved.
    [[nodiscard]] uint64_t stateFingerprint() const;

    aoc::game::GameState*             m_gameState        = nullptr;
    const aoc::map::HexGrid*          m_grid             = nullptr;
    const aoc::sim::DiplomacyManager* m_diplomacy        = nullptr;
    PlayerId                          m_player           = INVALID_PLAYER;
    WidgetId                          m_summaryLabel     = INVALID_WIDGET;
    WidgetId                          m_list             = INVALID_WIDGET;
    uint64_t                          m_shownFingerprint = 0;
};

} // namespace aoc::ui
