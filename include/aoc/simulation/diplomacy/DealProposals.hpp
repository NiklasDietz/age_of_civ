/**
 * @file DealProposals.hpp
 * @brief Deal proposals with an inbox for the human and a gold-equivalent valuation for the AI.
 *
 * proposeDeal/acceptDeal (DealTerms.hpp) apply a deal; nothing before this asked
 * the other side. requestProposeDeal now routes a proposal: to a human recipient
 * (Player::isHuman; nobody in a headless run)
 * it waits in GameState::pendingProposals() for PROPOSAL_TTL_TURNS and the human
 * answers with requestRespondToProposal; an AI recipient accepts at once when
 * dealValueFor says the deal is worth at least nothing to it, else declines.
 * Convention: `deal.playerA` proposes, `deal.playerB` receives.
 */

#pragma once

#include "aoc/core/ErrorCodes.hpp"
#include "aoc/core/Types.hpp"
#include "aoc/simulation/diplomacy/DealTerms.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace aoc::game {
class GameState;
}
namespace aoc::map {
class HexGrid;
}

namespace aoc::sim {

class DiplomacyManager;

/// Net worth of `deal` in gold for `evaluator`: transfers count by direction
/// (gold at face value, reparations per turn times duration, a city 200 + 50
/// per citizen, a tile 40); pacts by stance (open borders and mutual defence
/// need Friendly or Allied, non-aggression is welcome unless Hostile, arms
/// limits and zones cost, war guilt costs the side that accepts blame); plus a
/// quarter of a positive relation score as goodwill.
[[nodiscard]] int32_t dealValueFor(const aoc::game::GameState& gameState, const DiplomacyManager& diplomacy,
                                   PlayerId evaluator, const DiplomaticDeal& deal);

[[nodiscard]] bool aiAcceptsDeal(const aoc::game::GameState& gameState, const DiplomacyManager& diplomacy,
                                 PlayerId ai, const DiplomaticDeal& deal);

/// "100 gold (Rome -> Egypt)", "Open Borders (30 turns)", ...
[[nodiscard]] std::string describeDealTerm(const aoc::game::GameState& gameState, const DealTerm& term);

/// EntityNotFound: bad or identical parties. InvalidState: not met, a pending
/// proposal already waits between them, open borders offered at war, or the AI
/// declines. InvalidArgument: no terms or a term naming a third party. For an AI
/// recipient the deal is applied through acceptDeal and its code is returned.
ErrorCode requestProposeDeal(aoc::game::GameState& gameState, aoc::map::HexGrid& grid, GlobalDealTracker& tracker,
                             const DiplomacyManager& diplomacy, const DiplomaticDeal& deal, int32_t currentTurn);

/// The human answers proposal `index` of GameState::pendingProposals(). The
/// proposal leaves the inbox either way; on accept the deal is applied through
/// acceptDeal and its code is returned.
ErrorCode requestRespondToProposal(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                                   GlobalDealTracker& tracker, PlayerId responder, std::size_t index, bool accept);

/// Drop proposals whose expiresTurn has come. Runs once per turn.
void expireProposals(aoc::game::GameState& gameState, int32_t currentTurn);

} // namespace aoc::sim
