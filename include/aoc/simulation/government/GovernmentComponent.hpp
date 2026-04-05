#pragma once

/**
 * @file GovernmentComponent.hpp
 * @brief Per-player government state ECS component and modifier computation.
 *
 * Tracks the active government, equipped policy cards, and which governments
 * and policies have been unlocked through civic research.
 */

#include "aoc/simulation/government/Government.hpp"
#include "aoc/core/Types.hpp"

#include <array>
#include <cstdint>

namespace aoc::ecs {
class World;
}

namespace aoc::sim {

/// Maximum number of policy slots any government can have.
inline constexpr uint8_t MAX_POLICY_SLOTS = 5;

/// Sentinel value meaning "no policy equipped in this slot".
inline constexpr int8_t EMPTY_POLICY_SLOT = -1;

/// Per-player government state (ECS component).
struct PlayerGovernmentComponent {
    PlayerId       owner = INVALID_PLAYER;
    GovernmentType government = GovernmentType::Chiefdom;

    /// Active policy card IDs per slot. -1 = empty.
    std::array<int8_t, MAX_POLICY_SLOTS> activePolicies = {
        EMPTY_POLICY_SLOT, EMPTY_POLICY_SLOT, EMPTY_POLICY_SLOT,
        EMPTY_POLICY_SLOT, EMPTY_POLICY_SLOT
    };

    /// Bitfield of unlocked government types (1 bit per GovernmentType).
    uint8_t unlockedGovernments = 1u;  ///< Chiefdom (bit 0) always unlocked.

    /// Bitfield of unlocked policy card IDs (1 bit per policy, up to 16).
    uint16_t unlockedPolicies = 0u;

    /// Check if a government type has been unlocked.
    [[nodiscard]] bool isGovernmentUnlocked(GovernmentType type) const {
        return (this->unlockedGovernments & (1u << static_cast<uint8_t>(type))) != 0u;
    }

    /// Unlock a government type.
    void unlockGovernment(GovernmentType type) {
        this->unlockedGovernments |= static_cast<uint8_t>(1u << static_cast<uint8_t>(type));
    }

    /// Check if a policy card has been unlocked.
    [[nodiscard]] bool isPolicyUnlocked(uint8_t policyId) const {
        return (this->unlockedPolicies & static_cast<uint16_t>(1u << policyId)) != 0u;
    }

    /// Unlock a policy card.
    void unlockPolicy(uint8_t policyId) {
        this->unlockedPolicies |= static_cast<uint16_t>(1u << policyId);
    }
};

/**
 * @brief Compute the combined government modifiers for a player.
 *
 * Sums the government's inherent bonuses with all active policy card modifiers.
 * Multipliers are multiplied together; flat bonuses (combat) are added.
 *
 * @param world   ECS world with PlayerGovernmentComponent.
 * @param player  Player whose modifiers to compute.
 * @return Combined GovernmentModifiers.
 */
[[nodiscard]] GovernmentModifiers computeGovernmentModifiers(
    const aoc::ecs::World& world, PlayerId player);

} // namespace aoc::sim
