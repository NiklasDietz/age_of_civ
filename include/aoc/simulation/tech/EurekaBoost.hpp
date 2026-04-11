#pragma once

/**
 * @file EurekaBoost.hpp
 * @brief Eureka/Inspiration boosts that grant partial research progress
 *        when specific gameplay conditions are met.
 */

#include "aoc/core/Types.hpp"

#include <bitset>
#include <cstdint>
#include <string_view>
#include <vector>

namespace aoc::game { class Player; }

namespace aoc::sim {

// ============================================================================
// Eureka condition enum
// ============================================================================

/// Gameplay actions that can trigger a eureka boost.
enum class EurekaCondition : uint8_t {
    BuildQuarry,
    MeetCivilization,
    FoundCity,
    BuildCampus,
    KillUnit,
    BuildHarbor,
    ResearchTech,
    BuildWonder,
    TrainUnit,

    Count
};

static constexpr uint8_t EUREKA_CONDITION_COUNT =
    static_cast<uint8_t>(EurekaCondition::Count);

// ============================================================================
// Eureka boost definition
// ============================================================================

/// A single eureka/inspiration boost definition.
struct EurekaBoostDef {
    uint16_t          boostIndex;      ///< Unique index into the boost table.
    TechId            techId;          ///< Tech this boost applies to (invalid = civic).
    CivicId           civicId;         ///< Civic this boost applies to (invalid = tech).
    EurekaCondition   condition;       ///< What triggers the boost.
    std::string_view  description;     ///< Human-readable description.
    float             boostFraction;   ///< Fraction of research cost granted (e.g. 0.5 = 50%).
};

/// Maximum number of eureka boosts in the game.
static constexpr uint16_t MAX_EUREKA_BOOSTS = 64;

// ============================================================================
// Per-player ECS component
// ============================================================================

/// Tracks which eureka boosts have already been triggered for this player.
struct PlayerEurekaComponent {
    PlayerId owner = INVALID_PLAYER;

    /// Bitfield of triggered boosts, indexed by boostIndex.
    std::bitset<MAX_EUREKA_BOOSTS> triggeredBoosts{};

    [[nodiscard]] bool hasTriggered(uint16_t boostIndex) const {
        if (boostIndex >= MAX_EUREKA_BOOSTS) {
            return true;  // Out of range => treat as already triggered
        }
        return this->triggeredBoosts.test(boostIndex);
    }

    void markTriggered(uint16_t boostIndex) {
        if (boostIndex < MAX_EUREKA_BOOSTS) {
            this->triggeredBoosts.set(boostIndex);
        }
    }
};

// ============================================================================
// Public API
// ============================================================================

/// Get the static table of all eureka boost definitions.
[[nodiscard]] const std::vector<EurekaBoostDef>& getEurekaBoosts();

/**
 * @brief Check and apply eureka boosts when a gameplay action occurs.
 *
 * Iterates all boost definitions matching the given condition. For each
 * matching boost that hasn't been triggered yet, applies the boost fraction
 * as progress to the player's current or applicable research.
 *
 * @param world    ECS world containing player tech/civic/eureka components.
 * @param player   The player who performed the action.
 * @param triggered The gameplay condition that just occurred.
 */
void checkEurekaConditions(aoc::game::Player& player,
                           EurekaCondition triggered);

} // namespace aoc::sim
