#pragma once

/**
 * @file ScoreScreen.hpp
 * @brief End-game score screen showing breakdown by category for all players.
 */

#include "aoc/ui/GameScreens.hpp"
#include "aoc/simulation/victory/VictoryCondition.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace aoc::ui {

/// Per-player score breakdown shown on the end-game screen.
struct PlayerScoreEntry {
    PlayerId owner         = INVALID_PLAYER;
    int32_t  military      = 0;   ///< units * combat strength
    int32_t  science       = 0;   ///< techs researched * 10
    int32_t  culture       = 0;   ///< total culture accumulated
    int32_t  economy       = 0;   ///< treasury + GDP proxy
    int32_t  cities        = 0;   ///< population * 5 + city count * 20
    int32_t  religion      = 0;   ///< follower cities * 5
    int32_t  wonders       = 0;   ///< wonders built * 15
    int32_t  total         = 0;   ///< sum of all categories
};

/// End-game score screen displayed when a victory condition is met.
class ScoreScreen final : public ScreenBase {
public:
    /// Set context before opening. Must be called each time before open().
    void setContext(aoc::ecs::World* world,
                    const aoc::map::HexGrid* grid,
                    const aoc::sim::VictoryResult& result,
                    uint8_t playerCount,
                    std::function<void()> onReturnToMenu);

    void open(UIManager& ui) override;
    void close(UIManager& ui) override;
    void refresh(UIManager& ui) override;

private:
    /// Compute score entries for all players from current game state.
    void computeScores();

    aoc::ecs::World*       m_world          = nullptr;
    const aoc::map::HexGrid* m_grid         = nullptr;
    aoc::sim::VictoryResult m_victoryResult  = {};
    uint8_t                m_playerCount     = 0;
    std::function<void()>  m_onReturnToMenu;
    std::vector<PlayerScoreEntry> m_scores;
};

} // namespace aoc::ui
