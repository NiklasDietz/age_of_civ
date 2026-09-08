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
 *
 * Every seat votes automatically when a resolution is proposed; a player may
 * replace its own vote until the tally (`requestCongressVote`) and register
 * what it proposes the next time it is chosen (`requestCongressProposal`).
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/Random.hpp"
#include "aoc/core/ErrorCodes.hpp"

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

/// Favor economics of a session, shared with the screen and the debug routes.
inline constexpr int32_t WORLD_CONGRESS_PROPOSAL_COST    = 30;  ///< Spent by the proposer
inline constexpr int32_t WORLD_CONGRESS_EXTRA_VOTE_COST  = 10;  ///< Per point of weight beyond the free first
inline constexpr int32_t WORLD_CONGRESS_MAX_VOTE_WEIGHT  = 4;   ///< 1 free + 3 bought
inline constexpr int32_t WORLD_CONGRESS_SESSION_INTERVAL = 30;  ///< Turns between sessions

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
    /// Magnitude = 1 + purchased extras (max 4). Sized to MAX_PLAYERS so every
    /// seat can vote; the static_assert below keeps the two in lockstep.
    std::array<int16_t, MAX_PLAYERS> votes{};
    static_assert(std::tuple_size_v<decltype(votes)> == MAX_PLAYERS,
                  "votes must be sized to MAX_PLAYERS so every seat can vote");
    std::vector<Resolution>       passedResolutions;
    std::vector<ActiveResolution> activeEffects;
    /// Seats whose vote this session came from `requestCongressVote` rather than
    /// the automatic utility vote; cleared together with `votes`.
    std::array<bool, MAX_PLAYERS> voteChosen{};
    /// Standing proposal registered through `requestCongressProposal`: used
    /// instead of the utility pick the next time `preferredBy` is chosen as
    /// proposer, then cleared. One seat at a time; the AI never sets it.
    Resolution preferredProposal = Resolution::Count;
    PlayerId   preferredTarget   = INVALID_PLAYER;
    PlayerId   preferredBy       = INVALID_PLAYER;

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

/// Replace `player`'s vote on the open proposal with a signed `weight` in
/// [-WORLD_CONGRESS_MAX_VOTE_WEIGHT, +WORLD_CONGRESS_MAX_VOTE_WEIGHT]; 0 abstains.
/// The first point is free and every extra point costs WORLD_CONGRESS_EXTRA_VOTE_COST
/// favor; extras the automatic vote already bought are refunded first, so changing
/// sides at the same weight is free. InvalidArgument for an unknown or eliminated
/// seat or an out-of-range weight, InvalidState when no proposal is open,
/// InsufficientResources when favor does not cover the added weight.
[[nodiscard]] ErrorCode requestCongressVote(aoc::game::GameState& gameState, PlayerId player,
                                            int32_t weight);

/// Register what `player` proposes the next time it is chosen as proposer (the
/// living seat with the most favor, at least WORLD_CONGRESS_PROPOSAL_COST).
/// GlobalSanctions needs a living rival as `target`; the prestige resolutions
/// always target the proposer and the rest take none, whatever is passed.
/// `Resolution::Count` clears the seat's registration. InvalidArgument otherwise.
[[nodiscard]] ErrorCode requestCongressProposal(aoc::game::GameState& gameState, PlayerId player,
                                                Resolution resolution, PlayerId target);

/// Process World Congress: accrue Favor, tick session timer, propose /
/// resolve resolutions, tick active effects.
/// Every other living civ refuses to trade with `target`, and the reverse.
///
/// One direction per civ: sanctioning one seat does not make it embargo the
/// world back. Until the embargo split these wrote both halves of every pair,
/// so a sanction fabricated N reciprocal embargoes against the voters.
///
/// Public because applying and lifting global sanctions is a coherent operation
/// in its own right, not only a step inside a resolution.
void applySanctionsBegin(DiplomacyManager* diplomacy, const aoc::game::GameState& gs,
                         PlayerId target);
void applySanctionsEnd(DiplomacyManager* diplomacy, const aoc::game::GameState& gs,
                       PlayerId target);

void processWorldCongress(aoc::game::GameState& gameState,
                          TurnNumber turn,
                          aoc::Random& rng,
                          DiplomacyManager* diplomacy);

} // namespace aoc::sim
