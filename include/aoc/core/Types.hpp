#pragma once

/**
 * @file Types.hpp
 * @brief Fundamental types used across the entire Age of Civilization project.
 */

#include <compare>
#include <cstdint>
#include <functional>
#include <limits>

namespace aoc {

// ============================================================================
// Entity identifier with generational index for safe reuse
// ============================================================================

struct EntityId {
    uint32_t index      : 20;  ///< Supports ~1M concurrent entities
    uint32_t generation : 12;  ///< 4096 reuse cycles before wrap

    constexpr bool operator==(const EntityId& other) const = default;

    [[nodiscard]] constexpr bool isValid() const {
        return this->index != INVALID_INDEX;
    }

    static constexpr uint32_t INVALID_INDEX = (1u << 20) - 1;  ///< 0xFFFFF
    static constexpr uint32_t MAX_ENTITIES  = INVALID_INDEX;    ///< ~1M
};

/// Sentinel value representing no entity.
inline constexpr EntityId NULL_ENTITY{EntityId::INVALID_INDEX, 0};

// ============================================================================
// Player / turn identifiers
// ============================================================================

using PlayerId    = uint8_t;
using TurnNumber  = uint32_t;

inline constexpr PlayerId   INVALID_PLAYER = std::numeric_limits<PlayerId>::max();
inline constexpr TurnNumber TURN_ZERO      = 0;

inline constexpr uint8_t MAX_PLAYERS = 16;

/// Special player ID for barbarian-controlled units and encampments.
inline constexpr PlayerId BARBARIAN_PLAYER = 255;

// ============================================================================
// Resource / economy quantities
// ============================================================================

using ResourceAmount = int32_t;   ///< Signed: deficits are negative
using CurrencyAmount = int64_t;   ///< Large monetary values
using Percentage     = float;     ///< Typically 0.0f .. 1.0f

// ============================================================================
// Strong typedef helpers for IDs loaded from data files
// ============================================================================

/// CRTP-based strong typedef for 16-bit identifiers.
template<typename Tag>
struct StrongId {
    uint16_t value = std::numeric_limits<uint16_t>::max();

    constexpr bool operator==(const StrongId& other) const = default;
    constexpr std::strong_ordering operator<=>(const StrongId& other) const = default;

    [[nodiscard]] constexpr bool isValid() const {
        return this->value != std::numeric_limits<uint16_t>::max();
    }
};

struct ResourceIdTag {};
struct ProcessedGoodIdTag {};
struct BuildingIdTag {};
struct UnitTypeIdTag {};
struct TechIdTag {};
struct CivicIdTag {};
struct DistrictTypeIdTag {};
struct PromotionIdTag {};
struct EraIdTag {};

using ResourceId      = StrongId<ResourceIdTag>;
using ProcessedGoodId = StrongId<ProcessedGoodIdTag>;
using BuildingId      = StrongId<BuildingIdTag>;
using UnitTypeId      = StrongId<UnitTypeIdTag>;
using TechId          = StrongId<TechIdTag>;
using CivicId         = StrongId<CivicIdTag>;
using DistrictTypeId  = StrongId<DistrictTypeIdTag>;
using PromotionId     = StrongId<PromotionIdTag>;
using EraId           = StrongId<EraIdTag>;

} // namespace aoc

// ============================================================================
// Hash specializations for use in unordered containers
// ============================================================================

template<>
struct std::hash<aoc::EntityId> {
    std::size_t operator()(const aoc::EntityId& id) const noexcept {
        // Pack into a single 32-bit value for hashing
        uint32_t packed = (static_cast<uint32_t>(id.generation) << 20) | id.index;
        return std::hash<uint32_t>{}(packed);
    }
};

template<typename Tag>
struct std::hash<aoc::StrongId<Tag>> {
    std::size_t operator()(const aoc::StrongId<Tag>& id) const noexcept {
        return std::hash<uint16_t>{}(id.value);
    }
};
