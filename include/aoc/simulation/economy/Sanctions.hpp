#pragma once

/**
 * @file Sanctions.hpp
 * @brief Economic sanctions with monetary system integration.
 *
 * Three tiers of sanctions (escalating severity):
 *
 * 1. Trade Embargo (existing): No trade routes between sanctioner and target.
 *
 * 2. Financial Sanctions (SWIFT-style):
 *    Cut the target off from your currency system. If you hold the reserve
 *    currency, this is devastating -- the target can't settle trades
 *    denominated in your currency with ANY player.
 *    Effect: target's trade efficiency drops 30% with all players using
 *    your currency as settlement medium.
 *
 * 3. Asset Freeze:
 *    Seize coin reserves and bond holdings that the target has in your
 *    territory (from trade settlement accumulation).
 *    Effect: target loses all coins/bonds held in your cities.
 *    This is a very aggressive action -- may trigger grievance/war.
 *
 * Secondary Sanctions:
 *    If you sanction a target, any third-party player who continues
 *    trading with the target gets a 15% trade penalty with you.
 *    Forces players to pick sides.
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/ErrorCodes.hpp"

#include <cstdint>
#include <vector>

namespace aoc::game { class GameState; }

namespace aoc::sim {

// ============================================================================
// Sanction types
// ============================================================================

enum class SanctionType : uint8_t {
    TradeEmbargo       = 0,   ///< No trade routes
    FinancialSanction  = 1,   ///< Cut off from currency system
    AssetFreeze        = 2,   ///< Seize target's financial assets

    Count
};

/// A single active sanction between two players.
struct SanctionEntry {
    PlayerId     sanctioner = INVALID_PLAYER;
    PlayerId     target = INVALID_PLAYER;
    SanctionType type = SanctionType::TradeEmbargo;
    int32_t      turnsActive = 0;
    bool         hasSecondary = false;  ///< If true, secondary sanctions apply to third parties
};

/// Global sanction tracker (one per game).
struct GlobalSanctionTracker {
    std::vector<SanctionEntry> activeSanctions;

    /// Check if player A has any sanction against player B.
    [[nodiscard]] bool isSanctioned(PlayerId sanctioner, PlayerId target) const {
        for (const SanctionEntry& s : this->activeSanctions) {
            if (s.sanctioner == sanctioner && s.target == target) {
                return true;
            }
        }
        return false;
    }

    /// Check for a specific sanction type.
    [[nodiscard]] bool hasSanction(PlayerId sanctioner, PlayerId target,
                                    SanctionType type) const {
        for (const SanctionEntry& s : this->activeSanctions) {
            if (s.sanctioner == sanctioner && s.target == target && s.type == type) {
                return true;
            }
        }
        return false;
    }

    /// Get the trade efficiency penalty for a player due to all sanctions against them.
    [[nodiscard]] float tradeEfficiencyPenalty(PlayerId target) const {
        float penalty = 1.0f;
        for (const SanctionEntry& s : this->activeSanctions) {
            if (s.target == target) {
                switch (s.type) {
                    case SanctionType::TradeEmbargo:
                        // Handled at trade route creation level
                        break;
                    case SanctionType::FinancialSanction:
                        penalty *= 0.70f;  // -30% per financial sanction
                        break;
                    case SanctionType::AssetFreeze:
                        penalty *= 0.85f;  // -15% (less ongoing, but initial damage is done)
                        break;
                    default:
                        break;
                }
            }
        }
        return penalty;
    }

    /// Check if a third-party player is subject to secondary sanctions
    /// for trading with a sanctioned target.
    [[nodiscard]] float secondarySanctionPenalty(PlayerId thirdParty,
                                                  PlayerId tradingWith) const {
        float penalty = 1.0f;
        for (const SanctionEntry& s : this->activeSanctions) {
            if (s.target == tradingWith && s.hasSecondary && s.sanctioner != thirdParty) {
                penalty *= 0.85f;  // -15% for each sanctioner with secondary sanctions
            }
        }
        return penalty;
    }
};

// ============================================================================
// Sanction operations
// ============================================================================

/**
 * @brief Impose a sanction on a target player.
 *
 * @param world       ECS world.
 * @param tracker     Global sanction tracker.
 * @param sanctioner  Player imposing the sanction.
 * @param target      Player being sanctioned.
 * @param type        Type of sanction.
 * @param secondary   Whether to apply secondary sanctions to third parties.
 * @return Ok if successful.
 */
[[nodiscard]] ErrorCode imposeSanction(aoc::game::GameState& gameState,
                                       GlobalSanctionTracker& tracker,
                                       PlayerId sanctioner, PlayerId target,
                                       SanctionType type, bool secondary);

/**
 * @brief Lift a specific sanction.
 */
void liftSanction(GlobalSanctionTracker& tracker,
                  PlayerId sanctioner, PlayerId target,
                  SanctionType type);

/**
 * @brief Execute an asset freeze: seize the target's coins and bonds
 * held in the sanctioner's territory.
 *
 * This is called once when the asset freeze sanction is first imposed.
 *
 * @param world       ECS world.
 * @param sanctioner  Player seizing assets.
 * @param target      Player whose assets are being seized.
 */
void executeAssetFreeze(aoc::game::GameState& gameState,
                        PlayerId sanctioner, PlayerId target);

/**
 * @brief Per-turn processing of sanctions (tick durations, apply effects).
 */
void processSanctions(aoc::game::GameState& gameState, GlobalSanctionTracker& tracker);

} // namespace aoc::sim
