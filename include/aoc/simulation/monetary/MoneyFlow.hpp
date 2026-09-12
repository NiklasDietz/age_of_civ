/**
 * @file MoneyFlow.hpp
 * @brief The one money seam (plan B1): every treasury mutation names where
 *        the money comes from or goes, and a transient ledger books what
 *        entered or left the world each turn, so conservation can be checked.
 */
#pragma once

#include "aoc/core/Types.hpp"

#include <array>
#include <cstdint>

namespace aoc::game {
class GameState;
class Player;
}

namespace aoc::sim {

/// Where money comes from or goes when a treasury moves. Every Player
/// treasury mutator takes one, with no default, so a call site has to say.
enum class MoneyFlowKind : uint8_t {
    Domestic, ///< Between this treasury and a civ's private money (own civ unless said otherwise)
    Transfer, ///< Between two treasuries; the other civ books its own side
    External, ///< The external sector: city-states, barbarian hoards, goody huts, endowments
    Minted,   ///< New coin from the Mint (the sweep's seigniorage share)
    Printed,  ///< Notes issued against nothing (fiat)
    Loss,     ///< Money that leaves the world: melted, sunk, plundered by nobody
    Unbacked, ///< Money the old model conjures or destroys; Phase 2.2 retires every use
};

struct MoneyFlow {
    MoneyFlowKind kind    = MoneyFlowKind::Unbacked;
    PlayerId counterparty = INVALID_PLAYER; ///< Domestic: whose private money; Transfer: the other treasury

    [[nodiscard]] static constexpr MoneyFlow domestic(PlayerId civ) { return {MoneyFlowKind::Domestic, civ}; }
    [[nodiscard]] static constexpr MoneyFlow transfer(PlayerId other) { return {MoneyFlowKind::Transfer, other}; }
    [[nodiscard]] static constexpr MoneyFlow external() { return {MoneyFlowKind::External, INVALID_PLAYER}; }
    [[nodiscard]] static constexpr MoneyFlow minted() { return {MoneyFlowKind::Minted, INVALID_PLAYER}; }
    [[nodiscard]] static constexpr MoneyFlow printed() { return {MoneyFlowKind::Printed, INVALID_PLAYER}; }
    [[nodiscard]] static constexpr MoneyFlow loss() { return {MoneyFlowKind::Loss, INVALID_PLAYER}; }
    [[nodiscard]] static constexpr MoneyFlow unbacked() { return {MoneyFlowKind::Unbacked, INVALID_PLAYER}; }
};

/// What entered and left the world's money this turn, per civ. Transient:
/// reset every turn, never saved. Domestic and Transfer flows move money
/// inside the world and book nothing here.
struct MoneyLedger {
    struct Civ {
        int64_t minted      = 0; ///< face value swept from the Mint's coin goods
        int64_t seigniorage = 0; ///< the treasury's share of that (part of minted)
        int64_t printed     = 0;
        int64_t externalIn  = 0;
        int64_t externalOut = 0;
        int64_t lost        = 0;
        int64_t unbackedIn  = 0; ///< conjured by the old model
        int64_t unbackedOut = 0; ///< destroyed by the old model
    };
    std::array<Civ, MAX_PLAYERS> civs{};

    void reset();
    /// `delta` is the change of the civ's treasury (or private money) under `flow`.
    void record(PlayerId who, MoneyFlow flow, CurrencyAmount delta);
    [[nodiscard]] Civ total() const;
    /// What the world's money should have changed by this turn, unbacked flows included.
    [[nodiscard]] int64_t expectedDelta() const;
    /// True when nothing was conjured or destroyed this turn.
    [[nodiscard]] bool backed() const;
};

/// A Barter civ has no money economy: it earns no tax, pays no upkeep and is
/// charged nothing for research; its bullion waits for coinage (plan Part E).
[[nodiscard]] bool moneyless(const aoc::game::Player& player);

/// The treasury pays up to `amount` into its own people's hands (a purchase,
/// a building's upkeep, research, a policy change). Never overdraws: what
/// the treasury lacks stays unpaid. Returns what was paid.
CurrencyAmount payFromTreasury(aoc::game::Player& payer, CurrencyAmount amount);

/// The same, into the hands of the civ holding the province `civ` (a garrison
/// abroad pays the locals): the payer's own people when `civ` is the payer or
/// nobody, the external sector when it is a city-state seat.
CurrencyAmount payFromTreasury(aoc::game::GameState& gameState, aoc::game::Player& payer,
                               CurrencyAmount amount, PlayerId civ);

/// The treasury draws up to `amount` from its own people (a tithe, a levy):
/// never more than they hold. Returns what was taken.
CurrencyAmount takeFromPrivate(aoc::game::Player& taker, CurrencyAmount amount);

/// `taker`'s treasury draws up to `amount` from the private money of `civ`'s
/// people (a tithe, a pillaged farm, a toll): never more than they hold, so
/// a civ with no money yields nothing. Returns what was taken.
CurrencyAmount takeFromPrivate(aoc::game::GameState& gameState, PlayerId civ,
                               aoc::game::Player& taker, CurrencyAmount amount);

/// Plunder: `captor` takes up to `amount` from `victim`'s people first, then
/// from its treasury. A victim outside the world (barbarians, nobody) makes
/// the whole amount external. Returns what was taken.
CurrencyAmount plunder(aoc::game::GameState& gameState, PlayerId victim, aoc::game::Player& captor,
                       CurrencyAmount amount);

/// The buyer's people pay up to `price` in specie into a purse the caller
/// holds (a Trader's carried coin). Returns what they could pay.
CurrencyAmount payInSpecie(aoc::game::Player& buyer, CurrencyAmount price);

/// The same in notes, from the buyer's paper.
CurrencyAmount payInNotes(aoc::game::Player& buyer, CurrencyAmount price);

/// Whether a sale between these two settles in notes (plan B2): both on a
/// paper regime, and the buyer's paper trusted (trust at least 0.5) or the
/// seller the reserve currency. Otherwise specie, and goods failing that.
[[nodiscard]] bool settlesInNotes(const aoc::game::Player& seller, const aoc::game::Player& buyer);

/// Notes the buyer hands over per unit of price: the sellers' rate over the
/// buyer's, capped to [0.5, 2] so no currency is worthless or priceless.
[[nodiscard]] float settlementRate(const aoc::game::Player& seller, const aoc::game::Player& buyer);

/// Coin (or notes, when `notes`) arriving home from a trade: a Barter civ's
/// coin goes to bullion (the metal waits for coinage); otherwise the
/// tax-rate share goes to the treasury and the rest to the merchants.
/// Returns the treasury's share.
CurrencyAmount receiveTradeCoin(aoc::game::Player& seller, CurrencyAmount amount, bool notes = false);

/// Coin (or notes) into the people's hands: loot, a windfall found on the road.
void giveToPrivate(aoc::game::Player& civ, CurrencyAmount amount, bool notes = false);

/// Coin leaving the world with its carrier (a Trader killed by barbarians,
/// expired abroad, deleted): booked as a loss on the owner's ledger. The
/// caller zeroes the purse.
void loseCoin(const aoc::game::Player& owner, CurrencyAmount coin);

/// Money that crossed into or out of the external sector (city-states,
/// barbarians, ruins) by a path that is not a treasury mutation: a purse
/// paid by a city-state buyer (+), a purse looted by a city-state (-).
void bookExternal(const aoc::game::Player& civ, CurrencyAmount delta);

/// Sum of every money pool in the world: treasuries, private specie and
/// notes, bullion, and coin carried by Traders on the road.
[[nodiscard]] int64_t worldMoney(const aoc::game::GameState& gameState);

/// The invariant for one turn: the world's money changed by exactly what the
/// ledger books, and none of it was conjured or destroyed.
[[nodiscard]] bool moneyConserved(int64_t worldMoneyBefore, int64_t worldMoneyAfter, const MoneyLedger& ledger);

/// Treasury share of freshly minted coin, the rest is the minter's private money.
inline constexpr int32_t SEIGNIORAGE_PCT = 10;

} // namespace aoc::sim
