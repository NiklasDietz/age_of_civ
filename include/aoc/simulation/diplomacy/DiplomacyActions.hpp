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

/// War weariness at which an AI takes peace even while winning.
///
/// aiAcceptsPeace only ever asked whether the AI was LOSING, and
/// requestMakePeace asks the side being sued. So the winner was asked, and by
/// construction refused: there was no path out of a war for the losing side and
/// no path for a victor to grant terms. Wars ran to annihilation. Weariness
/// accrues at 1/turn per enemy, so this is a war of about thirty turns -- the
/// same span as the AI's own campaign commitment cap, past which it
/// re-evaluates the campaign anyway.
inline constexpr float   PEACE_WEARINESS_TURNS  = 30.0f;
inline constexpr int32_t DENOUNCE_TURNS         = 30; ///< A denouncement, and its Formal War, lasts this long
inline constexpr int32_t FRIENDSHIP_TURNS       = 30;
inline constexpr int32_t OPEN_BORDERS_TURNS     = 30;
inline constexpr int32_t DELEGATION_GOLD        = 25;
inline constexpr int32_t EMBASSY_GOLD           = 50;
inline constexpr int32_t FRIENDSHIP_MIN_SCORE   = 10; ///< Friendly stance
inline constexpr int32_t OPEN_BORDERS_MIN_SCORE = 10;
inline constexpr int32_t COLONIAL_ERA_GAP       = 2;

/// Trade is a reason not to fight (plan B6, 4.2).
///
/// economicCostOfWar values what a civ stands to lose by fighting a partner:
/// the decayed "Trade partner" tie that every delivery refreshes, which already
/// carries both the value shipped and the routes running, plus the gold
/// standing deals pay it each turn and the goods it leans on them to ship.
/// warCostRelationPoints turns that into relation points, weighted by how much
/// the leader cares about commerce at all. An AI declaration is refused above
/// WAR_COST_VETO_POINTS, and a war worth PEACE_TRADE_VALUE a turn is one the AI
/// will end when asked.
inline constexpr int32_t WAR_COST_MAX_POINTS    = 30;
inline constexpr int32_t WAR_COST_VALUE_DIVISOR = 4;
inline constexpr int32_t WAR_COST_VETO_POINTS   = 10;
inline constexpr int32_t PEACE_TRADE_VALUE      = 8;
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
[[nodiscard]] bool aiAcceptsPeace(const aoc::game::GameState& gameState,
                                  const DiplomacyManager& diplomacy, PlayerId ai, PlayerId other);

/// What `me` would lose by going to war with `target`, in gold a turn.
[[nodiscard]] int32_t economicCostOfWar(const aoc::game::GameState& gameState,
                                        const DiplomacyManager& diplomacy, PlayerId me,
                                        PlayerId target);

/// economicCostOfWar as relation points, weighted by the leader's economicFocus
/// and capped at WAR_COST_MAX_POINTS. Zero when the two do not trade.
[[nodiscard]] int32_t warCostRelationPoints(const aoc::game::GameState& gameState,
                                            const DiplomacyManager& diplomacy, PlayerId me,
                                            PlayerId target);

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
