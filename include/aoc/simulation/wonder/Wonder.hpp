#pragma once

/// @file Wonder.hpp
/// @brief World wonder definitions and tracking.

#include "aoc/core/Types.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace aoc::sim {

using WonderId = uint8_t;
inline constexpr uint8_t WONDER_COUNT = 24;

struct WonderEffect {
    float productionMultiplier = 1.0f;
    float scienceBonus         = 0.0f;
    float cultureBonus         = 0.0f;
    float goldBonus            = 0.0f;
    float amenityBonus         = 0.0f;
    float faithBonus           = 0.0f;
};

struct WonderDef {
    WonderId         id;
    std::string_view name;
    EraId            era;
    int32_t          productionCost;
    TechId           prerequisiteTech;    ///< INVALID = no prereq
    WonderEffect     effect;
    std::string_view description;
};

/// Get all wonder definitions.
[[nodiscard]] const std::array<WonderDef, WONDER_COUNT>& allWonderDefs();

/// Look up a single wonder definition by ID.
[[nodiscard]] const WonderDef& wonderDef(WonderId id);

/// WP-A7 wonder era-decay multiplier. Wonders built in an earlier era decay
/// on a half-life of ~4 eras so Classical wonders still matter in Modern
/// rather than being dominated by flat Modern-era bonuses. Floor at 0.35
/// (35%) to keep every wonder a non-trivial contributor.
///   delta = max(0, currentEra.value - wonder.era.value)
///   factor = max(0.35, pow(0.85, delta))
[[nodiscard]] float wonderEraDecayFactor(const WonderDef& wdef, EraId currentEra);

/// ECS component tracking which wonders have been built globally (one per game).
struct GlobalWonderTracker {
    std::array<PlayerId, WONDER_COUNT> builtBy;  ///< INVALID_PLAYER = not yet built

    GlobalWonderTracker() { this->builtBy.fill(INVALID_PLAYER); }

    [[nodiscard]] bool isBuilt(WonderId id) const {
        return this->builtBy[id] != INVALID_PLAYER;
    }

    void markBuilt(WonderId id, PlayerId player) {
        this->builtBy[id] = player;
    }
};

/// ECS component on city entities listing which wonders it contains.
struct CityWondersComponent {
    std::vector<WonderId> wonders;

    [[nodiscard]] bool hasWonder(WonderId id) const {
        for (const WonderId w : this->wonders) {
            if (w == id) return true;
        }
        return false;
    }
};

} // namespace aoc::sim
