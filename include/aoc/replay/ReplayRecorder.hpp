#pragma once

/**
 * @file ReplayRecorder.hpp
 * @brief Records per-turn snapshots of key game metrics for post-game replay.
 *
 * A snapshot is recorded each turn. At end-game, the ScoreScreen can show
 * graphs of these values over time.
 */

#include "aoc/core/Types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace aoc::game {
class GameState;
}

namespace aoc::replay {

struct ReplayFrame {
    TurnNumber turn = 0;

    struct PlayerSnapshot {
        PlayerId owner      = INVALID_PLAYER;
        int32_t  score      = 0;
        int32_t  territory  = 0;
        int32_t  military   = 0;
        int32_t  population = 0;
        int32_t  techs      = 0;
    };

    std::vector<PlayerSnapshot> players;
};

class ReplayRecorder {
public:
    /// Record a snapshot of the current game state.
    void recordFrame(const aoc::game::GameState& gameState, TurnNumber turn);

    /// Clear all recorded frames.
    void clear();

    /// Access all recorded frames.
    [[nodiscard]] const std::vector<ReplayFrame>& frames() const;

    /// Save replay data to a binary file.
    void save(const std::string& filepath) const;

    /// Load replay data from a binary file.
    bool load(const std::string& filepath);

private:
    std::vector<ReplayFrame> m_frames;
};

} // namespace aoc::replay
