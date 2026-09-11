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
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace aoc::game {
class GameState;
class Player;
}
namespace aoc::map {
class HexGrid;
}

namespace aoc::sim {

class DiplomacyManager;
class Market;

/// Net worth of `deal` in gold for `evaluator`: transfers count by direction
/// (gold at face value, reparations per turn times duration, a city 200 + 50
/// per citizen, a tile 40); pacts by stance (open borders and mutual defence
/// need Friendly or Allied, non-aggression is welcome unless Hostile, arms
/// limits and zones cost, war guilt costs the side that accepts blame); plus a
/// quarter of a positive relation score as goodwill.
/// Which side of a goods transfer the evaluator stands on.
enum class GoodsSide : uint8_t { Receive, Give };

/// The valuation seam for every goods term (plan B4): market price anchor,
/// then percent factors. Receive side: a good the evaluator needs and lacks
/// entirely is worth GOODS_MISSING_PCT, one it holds less of than it needs
/// GOODS_SHORT_PCT, one it neither needs nor holds GOODS_NEUTRAL_PCT, one it
/// has in surplus GOODS_SURPLUS_PCT. Give side: parting with what leaves the
/// evaluator short costs GIVE_SHORTFALL_PCT, a thin surplus GIVE_THIN_PCT, a
/// deep one GIVE_SURPLUS_PCT. A giver who is the sole source on the map adds
/// SOLE_SOURCE_PCT, a strategic good at war WAR_STRATEGIC_PCT, and a giver
/// below CASH_POOR_TREASURY sells at CASH_POOR_PCT. Clamped to
/// [GOODS_VALUE_MIN_PCT, GOODS_VALUE_MAX_PCT] of the anchor. A null market
/// anchors on the goods table's base price.
inline constexpr int32_t GOODS_MISSING_PCT   = 250;
inline constexpr int32_t GOODS_SHORT_PCT     = 175;
inline constexpr int32_t GOODS_NEUTRAL_PCT   = 100;
inline constexpr int32_t GOODS_SURPLUS_PCT   = 60;
inline constexpr int32_t GIVE_SHORTFALL_PCT  = 200;
inline constexpr int32_t GIVE_THIN_PCT       = 100;
inline constexpr int32_t GIVE_SURPLUS_PCT    = 70;
inline constexpr int32_t SOLE_SOURCE_PCT     = 150;
inline constexpr int32_t WAR_STRATEGIC_PCT   = 125;
inline constexpr int32_t CASH_POOR_PCT       = 80;
inline constexpr int32_t GOODS_VALUE_MIN_PCT = 25;
inline constexpr int32_t GOODS_VALUE_MAX_PCT = 300;
inline constexpr CurrencyAmount CASH_POOR_TREASURY = 50;

[[nodiscard]] int32_t goodsValueFor(const aoc::game::GameState& gameState, const DiplomacyManager& diplomacy,
                                    const Market* market, PlayerId evaluator, PlayerId counterparty,
                                    uint16_t goodId, int32_t amount, GoodsSide side);

[[nodiscard]] int32_t dealValueFor(const aoc::game::GameState& gameState, const DiplomacyManager& diplomacy,
                                   PlayerId evaluator, const DiplomaticDeal& deal,
                                   const Market* market = nullptr);

[[nodiscard]] bool aiAcceptsDeal(const aoc::game::GameState& gameState, const DiplomacyManager& diplomacy,
                                 PlayerId ai, const DiplomaticDeal& deal, const Market* market = nullptr);

/// "100 gold (Rome -> Egypt)", "Open Borders (30 turns)", "5 Silk (Rome -> Egypt)", ...
[[nodiscard]] std::string describeDealTerm(const aoc::game::GameState& gameState, const DealTerm& term);

/// The civ's display name, or "P<id>" for a seat without one.
[[nodiscard]] std::string civName(const aoc::game::GameState& gameState, PlayerId id);

/// EntityNotFound: bad or identical parties. InvalidState: not met, a pending
/// proposal already waits between them, open borders offered at war, or the AI
/// declines. InvalidArgument: no terms or a term naming a third party. For an AI
/// recipient the deal is applied through acceptDeal and its code is returned.
ErrorCode requestProposeDeal(aoc::game::GameState& gameState, aoc::map::HexGrid& grid, GlobalDealTracker& tracker,
                             DiplomacyManager& diplomacy, const DiplomaticDeal& deal, int32_t currentTurn,
                             const Market* market = nullptr);

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

/// `buyer` offers gold for `qty` of `goodId` to the first met, peaceful seller
/// that holds the stock and agrees. A human seller finds the offer in the
/// inbox; an AI seller answers by its own valuation. The AI used to call
/// proposeDeal and acceptDeal itself, which lifted goods out of a human's
/// cities without a prompt. True when a proposal was delivered or applied.
bool aiOfferToBuy(aoc::game::GameState& gameState, aoc::map::HexGrid& grid, GlobalDealTracker& tracker,
                  DiplomacyManager& diplomacy, PlayerId buyer, uint16_t goodId, int32_t qty,
                  int32_t currentTurn, const Market* market = nullptr);

/// Units of a luxury an AI buys at once: enough turns of LUXURY_UPKEEP that
/// the variety it buys outlasts the Trader that could have carried it.
inline constexpr int32_t LUXURY_DEAL_UNITS = 10;

/// An AI seller's contract: this many turns, at most this much bulk per
/// turn, offered only while the seller can spare CONTRACT_COVER_TURNS of it.
inline constexpr int32_t CONTRACT_OFFER_TURNS   = 30;
inline constexpr int32_t CONTRACT_BULK_PER_TURN = 5;
inline constexpr int32_t CONTRACT_COVER_TURNS   = 10;
/// A sole source considers exclusive access to a good on turns where
/// (turn + good) is a multiple of this, so offers are spread out.
inline constexpr int32_t EXCLUSIVE_OFFER_PERIOD = 20;

/// `seller` offers one supply contract this turn: its deepest surplus (stock
/// above its own need, lowest id on ties) to the first met, peaceful civ with
/// an unmet need for it and no such contract already running. Luxuries go one
/// a turn, bulk at the buyer's need capped at CONTRACT_BULK_PER_TURN; the rate
/// splits the gain between the seller's give value and the buyer's receive
/// value, both by their own valuation. A human buyer finds it in the inbox.
/// True when a proposal was delivered or applied.
bool aiOfferGoods(aoc::game::GameState& gameState, aoc::map::HexGrid& grid, GlobalDealTracker& tracker,
                  DiplomacyManager& diplomacy, PlayerId seller, int32_t currentTurn,
                  const Market* market = nullptr);

/// On a good's staggered turn, a `seller` that is the only civ holding it
/// offers exclusive access to the first met, peaceful civ that needs it, for a
/// lump midway between the two valuations and never more than the buyer holds.
bool aiOfferExclusiveAccess(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                            GlobalDealTracker& tracker, DiplomacyManager& diplomacy, PlayerId seller,
                            int32_t currentTurn, const Market* market = nullptr);

struct PurchaseTarget {
    uint16_t goodId = 0;
    int32_t amount  = 0;
};

/// What an AI should try to buy this turn: the first luxury type it lacks
/// that exists somewhere (variety shortfall, lowest id first), else its
/// largest bulk need capped at GOODS_DEAL_MAX_UNITS (lowest id on ties).
/// The old rule took the largest quantity, which a luxury need of one could
/// never win.
[[nodiscard]] std::optional<PurchaseTarget> aiPurchaseTarget(const aoc::game::Player& buyer);

/// One good on the world market as `viewer` sees it: who holds how much and
/// who wants it, among the civs the viewer has met plus the viewer. Coin
/// goods are left out; the market view is about goods.
struct WorldMarketRow {
    uint16_t goodId = 0;
    std::vector<std::pair<PlayerId, int32_t>> holders; ///< ascending by player
    std::vector<PlayerId> seekers;                     ///< civs with an unmet need
};

/// Rows ascending by good id; a null diplomacy counts every civ as met.
[[nodiscard]] std::vector<WorldMarketRow> worldMarketRows(const aoc::game::GameState& gameState,
                                                          const DiplomacyManager* diplomacy,
                                                          PlayerId viewer);

/// Drop proposals whose expiresTurn has come. Runs once per turn.
void expireProposals(aoc::game::GameState& gameState, int32_t currentTurn);

} // namespace aoc::sim
