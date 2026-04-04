#pragma once

/**
 * @file Treaty.hpp
 * @brief Treaty types and deal proposal structures.
 */

#include "aoc/core/Types.hpp"
#include "aoc/simulation/economy/TradeRoute.hpp"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace aoc::sim {

enum class TreatyType : uint8_t {
    OpenBorders,
    DefensiveAlliance,
    TradeAgreement,
    PeaceTreaty,
    NonAggressionPact,
    ResearchAgreement,
};

/// A single term in a diplomatic deal.
struct DealTerm {
    std::variant<
        TreatyType,         ///< A treaty
        TradeOffer,         ///< A per-turn good transfer
        CurrencyAmount      ///< A lump-sum gold payment
    > content;
};

/// A full diplomatic deal proposal from one player to another.
struct DealProposal {
    PlayerId             proposer;
    PlayerId             recipient;
    std::vector<DealTerm> proposerOffers;   ///< What the proposer gives
    std::vector<DealTerm> recipientOffers;  ///< What the proposer wants
    int32_t              duration = 30;     ///< Turns until expiry
};

/// Response to a deal proposal.
enum class DealResponse : uint8_t {
    Accepted,
    Rejected,
    CounterProposal,
};

} // namespace aoc::sim
