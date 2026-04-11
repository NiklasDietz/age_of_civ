#pragma once

/**
 * @file WorldCongress.hpp
 * @brief World Congress system: global resolutions voted on by all players.
 *
 * Every 30 turns after the first session (turn 50), a random resolution is
 * proposed. All players vote, majority wins.
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/Random.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace aoc::game {
class GameState;
}

namespace aoc::sim {

enum class Resolution : uint8_t {
    BanNuclearWeapons,    ///< Prevents nuclear unit production
    GlobalSanctions,      ///< All players embargo target player
    WorldsFair,           ///< +50% culture for 10 turns for winner
    InternationalGames,   ///< +50% production for 10 turns for winner
    ArmsReduction,        ///< All players lose 2 military units
    ClimateAccord,        ///< All players -10% production from fossil fuels
    Count
};

/// Get a human-readable name for a resolution.
[[nodiscard]] constexpr const char* resolutionName(Resolution res) {
    switch (res) {
        case Resolution::BanNuclearWeapons:  return "Ban Nuclear Weapons";
        case Resolution::GlobalSanctions:    return "Global Sanctions";
        case Resolution::WorldsFair:         return "World's Fair";
        case Resolution::InternationalGames: return "International Games";
        case Resolution::ArmsReduction:      return "Arms Reduction";
        case Resolution::ClimateAccord:      return "Climate Accord";
        default:                             return "Unknown";
    }
}

struct WorldCongressComponent {
    bool isActive = false;
    int32_t turnsUntilNextSession = 50;  ///< First session at turn 50
    Resolution currentProposal = Resolution::Count;  ///< Count = no active proposal
    std::array<int8_t, 16> votes{};  ///< +1 yes, -1 no, 0 abstain per player
    std::vector<Resolution> passedResolutions;

    /// Propose a specific resolution for voting.
    void proposeResolution(Resolution res);

    /// Cast a vote for a player (+1 yes, -1 no, 0 abstain).
    void castVote(PlayerId player, int8_t vote);

    /// Tally votes and resolve. Returns true if the resolution passed.
    [[nodiscard]] bool resolveVotes();

    /// Check if a specific resolution has been passed.
    [[nodiscard]] bool isResolutionActive(Resolution res) const;
};

/// Process World Congress: decrement timer, propose resolutions, AI votes.
void processWorldCongress(aoc::game::GameState& gameState, TurnNumber turn, aoc::Random& rng);

} // namespace aoc::sim
