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
 * Applying a deal realizes its pacts (open borders for the term's duration) and,
 * when the parties were at war, makes peace: a deal concluded at war is the
 * peace treaty. Arrival, answer and expiry push Diplomacy notifications.
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
                             DiplomacyManager& diplomacy, const DiplomaticDeal& deal, int32_t currentTurn);

/// The human answers proposal `index` of GameState::pendingProposals(). The
/// proposal leaves the inbox either way; on accept the deal is applied through
/// acceptDeal and its code is returned.
ErrorCode requestRespondToProposal(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                                   GlobalDealTracker& tracker, DiplomacyManager& diplomacy, PlayerId responder,
                                   std::size_t index, bool accept, int32_t currentTurn);

/// AI offers (2.10c). Peace: the losing `loser` offers `winner` a tenth of its
/// treasury (or a non-aggression pact when broke); a human winner finds it in
/// the inbox, an AI winner values it. Open borders: offered when `ai` is at
/// least Friendly toward `other`; the other side consents by its own stance.
/// Both return true when the proposal was delivered or applied.
/// How long a beaten civ pays war reparations, in turns.
///
/// Reparations are a stream, not a lump: that is the difference between
/// DealTermType::WarReparations and DealTermType::GoldLump, and it is why the
/// term is enforced per turn.
inline constexpr int32_t REPARATIONS_DURATION_TURNS = 20;

bool aiOfferPeace(aoc::game::GameState& gameState, aoc::map::HexGrid& grid, GlobalDealTracker& tracker,
                  DiplomacyManager& diplomacy, PlayerId loser, PlayerId winner, int32_t currentTurn);
bool aiOfferOpenBorders(aoc::game::GameState& gameState, aoc::map::HexGrid& grid, GlobalDealTracker& tracker,
                        DiplomacyManager& diplomacy, PlayerId ai, PlayerId other, int32_t currentTurn);

/// Cap on a single negotiated shipment. A deal is a shipment, not a standing
/// supply contract.
inline constexpr int32_t GOODS_DEAL_MAX_UNITS = 20;

/// What a buyer offers as a percentage of the goods' base value. A seller
/// values them AT base price, so an offer that merely matches it gives them no
/// reason to agree; the premium is what makes the trade worth doing.
inline constexpr int32_t GOODS_DEAL_PREMIUM_PCT = 140;

/// `buyer` offers gold for `qty` of `goodId` to the first met, peaceful seller
/// that holds the stock and agrees. A human seller finds the offer in the
/// inbox; an AI seller answers by its own valuation. The AI used to call
/// proposeDeal and acceptDeal itself, which lifted goods out of a human's
/// cities without a prompt. True when a proposal was delivered or applied.
bool aiOfferToBuy(aoc::game::GameState& gameState, aoc::map::HexGrid& grid, GlobalDealTracker& tracker,
                  DiplomacyManager& diplomacy, PlayerId buyer, uint16_t goodId, int32_t qty,
                  int32_t currentTurn);

/// Drop proposals whose expiresTurn has come. Runs once per turn.
void expireProposals(aoc::game::GameState& gameState, int32_t currentTurn);

} // namespace aoc::sim
