#pragma once

/**
 * @file QualityTier.hpp
 * @brief Quality tiers for processed goods.
 *
 * Processed goods can be Standard, High, or Premium quality.
 * Quality is determined by:
 *   - Building level (Lv1 = Standard, Lv2 = chance of High, Lv3 = chance of Premium)
 *   - City production experience for the recipe
 *   - Whether the city has Precision Instruments in stockpile (bonus)
 *
 * Higher quality goods:
 *   - Sell for 1.5x (High) or 2.0x (Premium) market price
 *   - Some advanced recipes REQUIRE minimum quality inputs
 *   - Quality propagates up the chain: Premium steel -> better chance of Premium machinery
 */

#include "aoc/core/Types.hpp"

#include <cstdint>
#include <unordered_map>

namespace aoc::sim {

// ============================================================================
// Quality tier enum
// ============================================================================

enum class QualityTier : uint8_t {
    Standard = 0,
    High     = 1,
    Premium  = 2,

    Count
};

/// Price multiplier for each quality tier.
[[nodiscard]] constexpr float qualityPriceMultiplier(QualityTier tier) {
    switch (tier) {
        case QualityTier::Standard: return 1.0f;
        case QualityTier::High:     return 1.5f;
        case QualityTier::Premium:  return 2.0f;
        default:                    return 1.0f;
    }
}

// ============================================================================
// Per-city quality tracking for stockpiled goods
// ============================================================================

/// Tracks the quality distribution of goods in a city stockpile.
/// For each good, stores how many units are at each quality tier.
struct CityQualityComponent {
    struct QualityBreakdown {
        int32_t standard = 0;
        int32_t high     = 0;
        int32_t premium  = 0;

        [[nodiscard]] int32_t total() const {
            return this->standard + this->high + this->premium;
        }

        /// Average quality as a float (0.0 = all standard, 2.0 = all premium).
        [[nodiscard]] float averageQuality() const {
            int32_t t = this->total();
            if (t <= 0) { return 0.0f; }
            return static_cast<float>(this->high + this->premium * 2)
                 / static_cast<float>(t);
        }
    };

    /// Map: goodId -> quality breakdown.
    std::unordered_map<uint16_t, QualityBreakdown> qualities;

    /// Add produced goods at a specific quality tier.
    void addGoods(uint16_t goodId, int32_t amount, QualityTier tier) {
        QualityBreakdown& q = this->qualities[goodId];
        switch (tier) {
            case QualityTier::Standard: q.standard += amount; break;
            case QualityTier::High:     q.high += amount; break;
            case QualityTier::Premium:  q.premium += amount; break;
            default: q.standard += amount; break;
        }
    }

    /// Consume goods, preferring lowest quality first.
    /// Returns the average quality of the consumed goods.
    [[nodiscard]] float consumeGoods(uint16_t goodId, int32_t amount) {
        std::unordered_map<uint16_t, QualityBreakdown>::iterator it = this->qualities.find(goodId);
        if (it == this->qualities.end()) {
            return 0.0f;
        }
        QualityBreakdown& q = it->second;
        int32_t remaining = amount;
        float qualitySum = 0.0f;
        int32_t consumed = 0;

        // Consume standard first
        int32_t fromStandard = (remaining < q.standard) ? remaining : q.standard;
        q.standard -= fromStandard;
        remaining -= fromStandard;
        consumed += fromStandard;

        // Then high
        int32_t fromHigh = (remaining < q.high) ? remaining : q.high;
        q.high -= fromHigh;
        remaining -= fromHigh;
        qualitySum += static_cast<float>(fromHigh) * 1.0f;
        consumed += fromHigh;

        // Then premium
        int32_t fromPremium = (remaining < q.premium) ? remaining : q.premium;
        q.premium -= fromPremium;
        qualitySum += static_cast<float>(fromPremium) * 2.0f;
        consumed += fromPremium;

        return (consumed > 0) ? qualitySum / static_cast<float>(consumed) : 0.0f;
    }

    /// Get the breakdown for a good.
    [[nodiscard]] QualityBreakdown getBreakdown(uint16_t goodId) const {
        std::unordered_map<uint16_t, QualityBreakdown>::const_iterator it = this->qualities.find(goodId);
        return (it != this->qualities.end()) ? it->second : QualityBreakdown{};
    }
};

// ============================================================================
// Quality determination
// ============================================================================

/**
 * @brief Determine the quality tier of produced goods.
 *
 * @param buildingLevel     Current building level (1-3).
 * @param recipeExperience  Times this recipe has been executed in this city.
 * @param hasPrecisionInstr Whether the city has Precision Instruments in stockpile.
 * @param inputQuality      Average quality of input goods (0.0 = standard, 2.0 = premium).
 * @param turnHash          Deterministic hash for this turn+city (for pseudo-random quality roll).
 * @return Quality tier of the output.
 */
[[nodiscard]] QualityTier determineOutputQuality(
    int32_t buildingLevel,
    int32_t recipeExperience,
    bool hasPrecisionInstr,
    float inputQuality,
    uint32_t turnHash);

} // namespace aoc::sim
