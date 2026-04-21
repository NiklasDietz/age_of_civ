#pragma once

/**
 * @file DealTerms.hpp
 * @brief Diplomatic deals with specific terms, conditions, and enforcement.
 *
 * Deals are bilateral agreements with enforceable terms:
 *
 * Peace Treaty terms:
 *   - Cede cities (loser gives specific cities to winner)
 *   - War reparations (loser pays gold per turn for N turns)
 *   - Demilitarized zone (no military units within N tiles of border)
 *   - War guilt clause (loser accepts blame, +grievance)
 *
 * Trade Agreement terms:
 *   - Specific goods at specific rates for specific duration
 *   - Most-favored-nation (lowest tariff rate)
 *   - Exclusive resource access (only you can buy their iron)
 *
 * Non-Aggression Pact:
 *   - Neither side attacks for N turns
 *   - Breaking = massive grievance + diplomatic penalty with all civs
 *
 * Mutual Defense Pact:
 *   - If either side is attacked, the other joins the war
 *   - "Only if attacked" clause: doesn't trigger if your ally starts the war
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/ErrorCodes.hpp"
#include "aoc/map/HexCoord.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace aoc::game { class GameState; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

class DiplomacyManager;

// ============================================================================
// Deal term types
// ============================================================================

enum class DealTermType : uint8_t {
    // Peace terms
    CedeCity,           ///< Transfer a city to the other player
    CedeTile,           ///< Transfer a border-adjacent tile to the other player
    WarReparations,     ///< Pay gold per turn for N turns
    DemilitarizedZone,  ///< No military units within N tiles of border
    WarGuilt,           ///< Accept blame (+50 grievance)

    // Trade terms
    GoldLump,           ///< One-off gold transfer from fromPlayer to toPlayer
    GoodsExchange,      ///< Trade specific goods at fixed rate
    MostFavoredNation,  ///< Lowest tariff between parties
    ExclusiveAccess,    ///< Only buyer can purchase this resource

    // Pacts
    NonAggression,      ///< No attacks for N turns
    MutualDefense,      ///< Join war if ally is attacked (not aggressor)
    OpenBorders,        ///< Units can pass through territory
    ArmsLimitation,     ///< Cap on military unit count for both parties

    Count
};

struct DealTerm {
    DealTermType type;
    PlayerId     fromPlayer = INVALID_PLAYER;  ///< Who gives/accepts this term
    PlayerId     toPlayer = INVALID_PLAYER;     ///< Who receives/benefits

    // Type-specific data
    EntityId     cityEntity = NULL_ENTITY;      ///< For CedeCity
    aoc::hex::AxialCoord tileCoord{0, 0};       ///< For CedeTile
    int32_t      goldPerTurn = 0;               ///< For WarReparations
    int32_t      goldLump    = 0;               ///< For GoldLump
    int32_t      duration = 30;                 ///< Turns the term lasts
    uint16_t     goodId = 0;                    ///< For GoodsExchange
    int32_t      goodAmount = 0;                ///< For GoodsExchange
    int32_t      zoneRadius = 3;                ///< For DemilitarizedZone
    int32_t      maxMilitaryUnits = 0;          ///< For ArmsLimitation (0 = unlimited)
};

// ============================================================================
// Diplomatic deal
// ============================================================================

struct DiplomaticDeal {
    PlayerId             playerA;
    PlayerId             playerB;
    std::vector<DealTerm> terms;
    int32_t              turnsRemaining = 30;
    bool                 isAccepted = false;
    bool                 isBroken = false;

    /// Check if this deal contains a specific term type.
    [[nodiscard]] bool hasTerm(DealTermType type) const {
        for (const DealTerm& t : this->terms) {
            if (t.type == type) { return true; }
        }
        return false;
    }
};

/// Global deal tracker.
struct GlobalDealTracker {
    std::vector<DiplomaticDeal> activeDeals;

    /// Get all active deals involving a player.
    [[nodiscard]] std::vector<const DiplomaticDeal*> dealsInvolving(PlayerId player) const {
        std::vector<const DiplomaticDeal*> result;
        for (const DiplomaticDeal& deal : this->activeDeals) {
            if (deal.playerA == player || deal.playerB == player) {
                result.push_back(&deal);
            }
        }
        return result;
    }

    /// Check if two players have a non-aggression pact.
    [[nodiscard]] bool hasNonAggressionPact(PlayerId a, PlayerId b) const {
        for (const DiplomaticDeal& deal : this->activeDeals) {
            if (!deal.isAccepted || deal.isBroken) { continue; }
            if ((deal.playerA == a && deal.playerB == b)
                || (deal.playerA == b && deal.playerB == a)) {
                if (deal.hasTerm(DealTermType::NonAggression)) { return true; }
            }
        }
        return false;
    }

    /// Check if two players have a mutual defense pact.
    [[nodiscard]] bool hasMutualDefensePact(PlayerId a, PlayerId b) const {
        for (const DiplomaticDeal& deal : this->activeDeals) {
            if (!deal.isAccepted || deal.isBroken) { continue; }
            if ((deal.playerA == a && deal.playerB == b)
                || (deal.playerA == b && deal.playerB == a)) {
                if (deal.hasTerm(DealTermType::MutualDefense)) { return true; }
            }
        }
        return false;
    }
};

// ============================================================================
// Deal operations
// ============================================================================

/**
 * @brief Propose a deal to another player. AI auto-evaluates; human gets a popup.
 */
[[nodiscard]] ErrorCode proposeDeal(aoc::game::GameState& gameState,
                                    GlobalDealTracker& tracker,
                                    DiplomaticDeal deal);

/**
 * @brief Accept a proposed deal. Applies immediate terms (city cession, etc.).
 */
[[nodiscard]] ErrorCode acceptDeal(aoc::game::GameState& gameState,
                                   aoc::map::HexGrid& grid,
                                   GlobalDealTracker& tracker,
                                   int32_t dealIndex);

/**
 * @brief Break a deal (violate terms). Applies grievance and reputation penalties.
 *
 * NonAggression broken: -30 reputation with victim, -15 with ALL known players.
 * Other deals broken: -10 reputation with counterparty.
 */
void breakDeal(aoc::game::GameState& gameState, GlobalDealTracker& tracker,
               DiplomacyManager& diplomacy, PlayerId breaker, int32_t dealIndex);

/**
 * @brief Process active deals per turn (enforce terms, tick durations).
 *
 * Enforces: WarReparations (gold transfer), NonAggression (auto-break on war),
 * DemilitarizedZone (reputation penalty for military units in zone),
 * ArmsLimitation (reputation penalty for exceeding unit cap).
 */
void processDeals(aoc::game::GameState& gameState, GlobalDealTracker& tracker,
                  DiplomacyManager& diplomacy, const aoc::map::HexGrid& grid);

} // namespace aoc::sim
