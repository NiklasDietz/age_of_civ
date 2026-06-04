#pragma once

/**
 * @file AIDifficulty.hpp
 * @brief AI difficulty level for computer-controlled players.
 *
 * Lives in the simulation/ai layer because the AI controllers consume it. It
 * previously sat in the ui layer (MainMenu.hpp), which forced every AI header
 * to depend upward on the UI (and, transitively, the map generator). Keeping
 * it here lets the UI depend on the simulation, never the reverse.
 */

#include <cstdint>

namespace aoc::sim::ai {

/// AI difficulty level affecting AI bonuses/penalties.
enum class AIDifficulty : uint8_t {
    Easy,
    Normal,
    Hard,
};

} // namespace aoc::sim::ai
