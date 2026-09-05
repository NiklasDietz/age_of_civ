#pragma once

/**
 * @file GovernmentComponent.hpp
 * @brief Per-player government state ECS component and modifier computation.
 *
 * Tracks the active government, equipped policy cards, anarchy state,
 * and which governments and policies have been unlocked.
 */

#include "aoc/simulation/government/Government.hpp"
#include "aoc/core/Types.hpp"
#include "aoc/core/ErrorCodes.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

namespace aoc::game { class GameState; }
namespace aoc::game { class Player; }

namespace aoc::sim {

/// Maximum number of policy slots any government can have.
inline constexpr uint8_t MAX_POLICY_SLOTS = 6;

/// Sentinel value meaning "no policy equipped in this slot".
inline constexpr int8_t EMPTY_POLICY_SLOT = -1;

/// Per-player government state (ECS component).
struct PlayerGovernmentComponent {
    PlayerId       owner = INVALID_PLAYER;
    GovernmentType government = GovernmentType::Chiefdom;

    /// Active policy card IDs per slot. -1 = empty.
    std::array<int8_t, MAX_POLICY_SLOTS> activePolicies = {
        EMPTY_POLICY_SLOT, EMPTY_POLICY_SLOT, EMPTY_POLICY_SLOT,
        EMPTY_POLICY_SLOT, EMPTY_POLICY_SLOT, EMPTY_POLICY_SLOT
    };

    /// Bitfield of unlocked government types (1 bit per GovernmentType).
    uint16_t unlockedGovernments = 1u;  ///< Chiefdom (bit 0) always unlocked.

    /// Bitfield of unlocked policy card IDs (1 bit per policy, up to 32).
    uint32_t unlockedPolicies = 0u;

    /// Anarchy: turns remaining of no-bonus transition period.
    int32_t anarchyTurnsRemaining = 0;

    /// Active unique government action and its remaining duration.
    GovernmentAction activeAction = GovernmentAction::None;
    int32_t actionTurnsRemaining = 0;

    /// Player-enabled auto-policy manager: fill empty slots with best unlocked policies
    /// each turn using flat utility scoring. Ignored during anarchy.
    bool autoPolicies = false;

    /// Wildcard slots granted by buildings (Government Plaza). Derived every turn
    /// by processGovernment from the player's cities, so it is not saved; after a
    /// load it is back at the next turn. Read through wildcardSlotCount().
    uint8_t bonusWildcardSlots = 0;

    /// Check if a government type has been unlocked.
    [[nodiscard]] bool isGovernmentUnlocked(GovernmentType type) const {
        return (this->unlockedGovernments & static_cast<uint16_t>(1u << static_cast<uint8_t>(type))) != 0u;
    }

    /// Unlock a government type.
    void unlockGovernment(GovernmentType type) {
        this->unlockedGovernments |= static_cast<uint16_t>(1u << static_cast<uint8_t>(type));
    }

    /// Check if a policy card has been unlocked.
    [[nodiscard]] bool isPolicyUnlocked(uint8_t policyId) const {
        return (this->unlockedPolicies & (1u << policyId)) != 0u;
    }

    /// Unlock a policy card.
    void unlockPolicy(uint8_t policyId) {
        this->unlockedPolicies |= (1u << policyId);
    }

    /// Whether the player is currently in anarchy.
    [[nodiscard]] bool isInAnarchy() const {
        return this->anarchyTurnsRemaining > 0;
    }

    /// Change government (triggers anarchy).
    void changeGovernment(GovernmentType newGov) {
        if (newGov != this->government) {
            this->government = newGov;
            this->anarchyTurnsRemaining = ANARCHY_DURATION;
            // Clear all policy slots on government change
            for (int8_t& slot : this->activePolicies) {
                slot = EMPTY_POLICY_SLOT;
            }
        }
    }
};

/**
 * @brief Compute the combined government modifiers for a player.
 *
 * During anarchy: returns default modifiers (all 1.0, no bonuses).
 * Otherwise: sums government inherent bonuses with all active policy cards.
 *
 * @param world   ECS world with PlayerGovernmentComponent.
 * @param player  Player whose modifiers to compute.
 * @return Combined GovernmentModifiers.
 */
/// The wildcard policy slots the player actually has: the government's own plus
/// the building bonus, never pushing the total past MAX_POLICY_SLOTS.
[[nodiscard]] inline uint8_t wildcardSlotCount(const PlayerGovernmentComponent& gov) {
    const GovernmentDef& gdef = governmentDef(gov.government);
    const int32_t fixed = gdef.militarySlots + gdef.economicSlots + gdef.diplomaticSlots;
    const int32_t room  = static_cast<int32_t>(MAX_POLICY_SLOTS) - fixed;
    const int32_t want  = gdef.wildcardSlots + gov.bonusWildcardSlots;
    return static_cast<uint8_t>(std::clamp(want, 0, std::max(0, room)));
}

[[nodiscard]] GovernmentModifiers computeGovernmentModifiers(
    const aoc::game::GameState& gameState, PlayerId player);

/// Compute government modifiers directly from a government component.
[[nodiscard]] GovernmentModifiers computeGovernmentModifiers(
    const PlayerGovernmentComponent& gov);

/**
 * @brief Execute a government unique action.
 *
 * @param world   ECS world.
 * @param player  Player performing the action.
 * @return Ok if successful, InvalidArgument if action not available.
 */
[[nodiscard]] ErrorCode executeGovernmentAction(aoc::game::GameState& gameState, PlayerId player);

/**
 * @brief Per-turn government processing: tick anarchy, tick active actions.
 * Uses GameState Player directly (no ECS).
 */
void processGovernment(aoc::game::Player& player);

/**
 * @brief Fill empty policy slots with the highest-scoring unlocked policies.
 *
 * Respects slot types (Military/Economic/Diplomatic/Wildcard). Uses a flat
 * sum of modifier contributions; caller can scale the result afterwards via
 * a LeaderBehavior if gene-aware weighting is needed. Already-equipped slots
 * are left untouched so existing player choices are preserved.
 *
 * @param gov  Player government component to mutate.
 */
void equipBestPolicies(PlayerGovernmentComponent& gov);

} // namespace aoc::sim
