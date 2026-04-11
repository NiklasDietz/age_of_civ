#pragma once

/**
 * @file HumanCapital.hpp
 * @brief Education, literacy, and human capital development.
 *
 * Education is the long-term investment that compounds across eras:
 *
 *   - Literacy rate affects: science multiplier, production efficiency,
 *     government options, military effectiveness (complex weapons)
 *   - Uneducated population can't operate advanced buildings
 *   - Education reduces technological unemployment (workers retrain)
 *   - Brain drain: educated citizens migrate toward better economies
 *
 * Literacy grows from: Library, University, Research Lab, Campus district.
 * Decays slowly without these buildings (illiterate children replace
 * educated parents if no schools exist).
 *
 * Education tiers:
 *   0-20%: Illiterate (ancient era default)
 *   20-50%: Basic literacy (enables medieval-era buildings)
 *   50-75%: Educated workforce (enables industrial buildings)
 *   75-95%: Highly educated (enables information-era buildings)
 *   95%+: Universal education (maximum bonuses)
 */

#include "aoc/core/Types.hpp"

#include <cstdint>

namespace aoc::game { class GameState; }

namespace aoc::sim {

struct PlayerHumanCapitalComponent {
    PlayerId owner = INVALID_PLAYER;

    /// National literacy rate (0.0 to 1.0).
    float literacyRate = 0.05f;  // 5% baseline (ancient era)

    /// Science multiplier from education. Higher literacy = more effective research.
    [[nodiscard]] float scienceMultiplier() const {
        // 0% literacy: 0.5x science. 100% literacy: 1.5x science.
        return 0.50f + this->literacyRate;
    }

    /// Production efficiency from skilled workers.
    [[nodiscard]] float productionEfficiency() const {
        // Diminishing returns: uneducated workers are inefficient,
        // but you don't need PhDs to run a farm.
        if (this->literacyRate < 0.20f) { return 0.80f; }
        if (this->literacyRate < 0.50f) { return 0.90f; }
        if (this->literacyRate < 0.75f) { return 1.0f; }
        return 1.0f + (this->literacyRate - 0.75f) * 0.40f;  // Up to 1.1x at 100%
    }

    /// Military effectiveness from educated soldiers (complex weapons, tactics).
    [[nodiscard]] float militaryEfficiency() const {
        if (this->literacyRate < 0.30f) { return 0.90f; }
        return 0.90f + this->literacyRate * 0.10f;  // Up to 1.0 at 100%
    }

    /// Whether the population can operate buildings of a given era.
    [[nodiscard]] bool canOperateEraBuildings(int32_t eraValue) const {
        // Era 0-1 (Ancient/Classical): no education requirement
        // Era 2-3 (Medieval/Renaissance): need 20% literacy
        // Era 4 (Industrial): need 50% literacy
        // Era 5-6 (Modern/Information): need 75% literacy
        if (eraValue <= 1) { return true; }
        if (eraValue <= 3) { return this->literacyRate >= 0.20f; }
        if (eraValue <= 4) { return this->literacyRate >= 0.50f; }
        return this->literacyRate >= 0.75f;
    }
};

/**
 * @brief Update literacy rate based on education buildings.
 *
 * Literacy grows from Campus buildings (Library, University, Research Lab).
 * Each building adds capacity; literacy trends toward what the buildings
 * can support. Without buildings, literacy slowly decays.
 *
 * @param world   ECS world.
 * @param player  Player to update.
 */
void updateHumanCapital(aoc::game::GameState& gameState, PlayerId player);

} // namespace aoc::sim
