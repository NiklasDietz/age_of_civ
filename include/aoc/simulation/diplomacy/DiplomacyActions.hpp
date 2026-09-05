/**
 * @file DiplomacyActions.hpp
 * @brief Player-facing diplomacy requests: war with a casus belli, peace, denounce,
 *        friendship, delegation, embassy, open borders.
 *
 * Every request validates and returns an ErrorCode; the Diplomacy screen, the
 * debug routes and the MCP tools all call these instead of DiplomacyManager
 * directly, so the peace lock, the friendship guard and the AI's consent apply
 * to every path. EntityNotFound: bad or identical ids. InvalidState: not met,
 * wrong war state, a lock, a refusal or a duplicate. InvalidArgument: the casus
 * belli is not justified. InsufficientResources: gold.
 */

#pragma once

#include "aoc/core/ErrorCodes.hpp"
#include "aoc/core/Types.hpp"
#include "aoc/simulation/diplomacy/CasusBelli.hpp"

#include <cstdint>
#include <vector>

namespace aoc::game {
class GameState;
}

namespace aoc::sim {

class DiplomacyManager;
struct AllianceObligationTracker;

inline constexpr int32_t PEACE_LOCK_TURNS       = 10; ///< No new war this soon after peace
inline constexpr int32_t WAR_MIN_TURNS          = 10; ///< No peace this soon after declaring
inline constexpr int32_t DENOUNCE_TURNS         = 30; ///< A denouncement, and its Formal War, lasts this long
inline constexpr int32_t FRIENDSHIP_TURNS       = 30;
inline constexpr int32_t OPEN_BORDERS_TURNS     = 30;
inline constexpr int32_t DELEGATION_GOLD        = 25;
inline constexpr int32_t EMBASSY_GOLD           = 50;
inline constexpr int32_t FRIENDSHIP_MIN_SCORE   = 10; ///< Friendly stance
inline constexpr int32_t OPEN_BORDERS_MIN_SCORE = 10;
inline constexpr int32_t COLONIAL_ERA_GAP       = 2;
inline constexpr int32_t DENOUNCE_PENALTY       = -20;
inline constexpr int32_t FRIENDSHIP_BONUS       = 15;
inline constexpr int32_t DELEGATION_BONUS       = 3;
inline constexpr int32_t EMBASSY_BONUS          = 5;

/// Ok when `cb` is justified for `actor` against `target` right now:
/// Surprise always; Formal after a denouncement (DENOUNCE_TURNS) or a border
/// casus belli; Holy when the target's religion holds one of the actor's
/// cities; Liberation when the target holds a city founded by an ally of the
/// actor; Reconquest when it holds one the actor founded; Colonial when the
/// target is COLONIAL_ERA_GAP eras behind; Economic when the target embargoes
/// the actor. Protectorate is never justified yet (city-states are not in the
/// relation matrix). InvalidArgument otherwise.
[[nodiscard]] ErrorCode casusBelliUsable(const aoc::game::GameState& gameState,
                                         const DiplomacyManager& diplomacy, PlayerId actor,
                                         PlayerId target, CasusBelliType cb, int32_t currentTurn);
[[nodiscard]] std::vector<CasusBelliType> availableCasusBelli(const aoc::game::GameState& gameState,
                                                              const DiplomacyManager& diplomacy,
                                                              PlayerId actor, PlayerId target,
                                                              int32_t currentTurn);

/// The AI's own peace rule (AIDiplomacyController): it agrees when the other
/// side's military outnumbers its own beyond its personality threshold.
[[nodiscard]] bool aiAcceptsPeace(const aoc::game::GameState& gameState, PlayerId ai, PlayerId other);

ErrorCode requestDeclareWar(aoc::game::GameState& gameState, DiplomacyManager& diplomacy, PlayerId actor,
                            PlayerId target, CasusBelliType cb, int32_t currentTurn,
                            AllianceObligationTracker* obligations = nullptr);
ErrorCode requestMakePeace(aoc::game::GameState& gameState, DiplomacyManager& diplomacy, PlayerId actor,
                           PlayerId target, int32_t currentTurn);
ErrorCode requestDenounce(const aoc::game::GameState& gameState, DiplomacyManager& diplomacy,
                          PlayerId actor, PlayerId target, int32_t currentTurn);
ErrorCode requestDeclareFriendship(const aoc::game::GameState& gameState, DiplomacyManager& diplomacy,
                                   PlayerId actor, PlayerId target, int32_t currentTurn);
ErrorCode requestSendDelegation(aoc::game::GameState& gameState, DiplomacyManager& diplomacy,
                                PlayerId actor, PlayerId target);
ErrorCode requestEstablishEmbassy(aoc::game::GameState& gameState, DiplomacyManager& diplomacy,
                                  PlayerId actor, PlayerId target);
ErrorCode requestOpenBorders(const aoc::game::GameState& gameState, DiplomacyManager& diplomacy,
                             PlayerId actor, PlayerId target, int32_t currentTurn);

} // namespace aoc::sim
