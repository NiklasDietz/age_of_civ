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

/// Sum of every money pool in the world: treasuries, private specie and
/// notes, bullion, and coin carried by Traders on the road.
[[nodiscard]] int64_t worldMoney(const aoc::game::GameState& gameState);

/// The invariant for one turn: the world's money changed by exactly what the
/// ledger books, and none of it was conjured or destroyed.
[[nodiscard]] bool moneyConserved(int64_t worldMoneyBefore, int64_t worldMoneyAfter, const MoneyLedger& ledger);

/// Treasury share of freshly minted coin, the rest is the minter's private money.
inline constexpr int32_t SEIGNIORAGE_PCT = 10;

} // namespace aoc::sim
