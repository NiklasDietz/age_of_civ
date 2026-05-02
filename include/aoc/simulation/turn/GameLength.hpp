#pragma once

/**
 * @file GameLength.hpp
 * @brief Game length presets and turn scaling.
 *
 * Five game length presets that scale production costs, research costs,
 * and victory thresholds proportionally to keep per-turn pace constant
 * (~0.002 cost-units per turn at reference Standard).
 *
 *   Quick:    330 turns, 0.67x costs -- fast-paced, less depth
 *   Standard: 500 turns, 1.0x costs  -- balanced, full era progression
 *   Epic:     750 turns, 1.5x costs  -- extended mid-game, more diplomacy
 *   Marathon: 1500 turns, 3.0x costs -- every system gets deep use
 *   Eternal:  2000 turns, 4.0x costs -- full sandbox stress test
 *
 * The cost multiplier scales: production costs, research costs, civic costs,
 * wonder costs, and growth thresholds. This keeps the relative progression
 * the same across all lengths -- you still reach the Industrial era at
 * roughly the same percentage of the game.
 */

#include <cstdint>
#include <string_view>

namespace aoc::sim {

enum class GameLength : uint8_t {
    Quick,
    Standard,
    Epic,
    Marathon,
    Eternal,

    Count
};

struct GameLengthDef {
    GameLength       type;
    std::string_view name;
    int32_t          maxTurns;
    float            costMultiplier;    ///< Scales production, research, civic costs
    float            growthMultiplier;  ///< Scales food-for-growth thresholds
    int32_t          eraEvalInterval;   ///< Turns between era evaluations
    int32_t          integrationTurns;  ///< Turns needed for Global Integration Project
};

inline constexpr GameLengthDef GAME_LENGTH_DEFS[] = {
    //                          turns  cost   growth  era  integ
    {GameLength::Quick,    "Quick",     330, 0.67f, 1.00f, 15,  5},
    {GameLength::Standard, "Standard",  500, 1.00f, 1.00f, 30, 10},
    {GameLength::Epic,     "Epic",      750, 1.50f, 1.50f, 45, 15},
    {GameLength::Marathon, "Marathon", 1500, 3.00f, 3.00f, 60, 20},
    {GameLength::Eternal,  "Eternal",  2000, 4.00f, 4.00f, 75, 30},
};

inline constexpr int32_t GAME_LENGTH_COUNT = 5;

[[nodiscard]] inline constexpr const GameLengthDef& gameLengthDef(GameLength length) {
    return GAME_LENGTH_DEFS[static_cast<uint8_t>(length)];
}

/// Global game pace settings (set once at game start, affects all costs).
/// This is a singleton so that production/research systems can access it
/// without needing the full GameLengthDef passed through every function.
struct GamePace {
    float costMultiplier   = 1.0f;
    float growthMultiplier = 1.0f;
    int32_t eraInterval    = 30;

    static GamePace& instance() {
        static GamePace s_instance;
        return s_instance;
    }

    void setFromLength(GameLength length) {
        const GameLengthDef& def = gameLengthDef(length);
        this->costMultiplier = def.costMultiplier;
        this->growthMultiplier = def.growthMultiplier;
        this->eraInterval = def.eraEvalInterval;
    }
};

/// Parse a game length from string (for config files).
[[nodiscard]] inline GameLength parseGameLength(std::string_view name) {
    if (name == "Quick"    || name == "quick")    { return GameLength::Quick; }
    if (name == "Standard" || name == "standard") { return GameLength::Standard; }
    if (name == "Epic"     || name == "epic")     { return GameLength::Epic; }
    if (name == "Marathon" || name == "marathon")  { return GameLength::Marathon; }
    if (name == "Eternal"  || name == "eternal")   { return GameLength::Eternal; }
    return GameLength::Standard;
}

} // namespace aoc::sim
