#pragma once

/**
 * @file WorldCongress.hpp
 * @brief World Congress: proposer-driven resolutions funded by Diplomatic Favor.
 *
 * Every 30 turns after the first session (turn 50), the player with the
 * highest Favor stockpile proposes a resolution (spending 30 Favor). Each
 * player votes for/against by spending Favor (1 vote free, +10 per extra,
 * capped at 4 votes). Resolutions that pass apply real gameplay effects
 * with durations tracked in `activeEffects`.
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/Random.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace aoc::game { class GameState; }
namespace aoc::sim  { class DiplomacyManager; }

namespace aoc::sim {

enum class Resolution : uint8_t {
    BanNuclearWeapons,    ///< Prevents nuclear unit production
    GlobalSanctions,      ///< All players embargo target player for 20 turns
    WorldsFair,           ///< +30 prestige to proposer, culture flag for 10 turns
    InternationalGames,   ///< +30 prestige to proposer, production flag for 10 turns
    ArmsReduction,        ///< All players disband 2 weakest military units (one-shot)
    ClimateAccord,        ///< +30 prestige to proposer, production -10% fossil for 20 turns
    Count
};

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

/// An active, timed resolution effect. Non-timed binary flags (e.g.
/// BanNuclearWeapons) are represented by membership in `passedResolutions`.
struct ActiveResolution {
    Resolution type            = Resolution::Count;
    PlayerId   target          = INVALID_PLAYER;  ///< Sanctions target or boost recipient
    int32_t    turnsRemaining  = 0;
};

struct WorldCongressComponent {
    bool       isActive              = false;
    int32_t    turnsUntilNextSession = 50;            ///< First session at turn 50
    Resolution currentProposal       = Resolution::Count;  ///< Count = no active proposal
    PlayerId   proposer              = INVALID_PLAYER;     ///< Who spent Favor to propose
    PlayerId   proposalTarget        = INVALID_PLAYER;     ///< Resolved at proposal time (Sanctions/Boost)
    /// Signed per-player vote weight cast this session (+yes, -no, 0 abstain).
    /// Magnitude = 1 + purchased extras (max 4).
    std::array<int16_t, 16> votes{};
    std::vector<Resolution>       passedResolutions;
    std::vector<ActiveResolution> activeEffects;

    /// Start a proposal cycle.
    void proposeResolution(Resolution res, PlayerId proposer, PlayerId target = INVALID_PLAYER);

    /// Cast a weighted vote for a player.
    void castVote(PlayerId player, int16_t weight);

    /// Tally votes and resolve. Returns true if the resolution passed.
    [[nodiscard]] bool resolveVotes();

    /// True if a binary-flag resolution has ever passed.
    [[nodiscard]] bool isResolutionActive(Resolution res) const;

    /// True if a timed resolution is currently in force (target matches or INVALID_PLAYER wildcard).
    [[nodiscard]] bool isEffectActive(Resolution res, PlayerId target = INVALID_PLAYER) const;
};

/// Process World Congress: accrue Favor, tick session timer, propose /
/// resolve resolutions, tick active effects.
void processWorldCongress(aoc::game::GameState& gameState,
                          TurnNumber turn,
                          aoc::Random& rng,
                          DiplomacyManager* diplomacy);

} // namespace aoc::sim
