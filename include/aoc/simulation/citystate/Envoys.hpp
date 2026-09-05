/**
 * @file Envoys.hpp
 * @brief Per-player envoy pool: earned from civics, spent one at a time on city-states.
 *
 * Civ VI hands envoys out through civics and influence points; here every
 * completed civic grants ENVOYS_PER_CIVIC. Nothing accrues passively any more,
 * so suzerainty has to be bought with envoys (CityState.hpp computeSuzerain).
 */

#pragma once

#include <cstdint>

namespace aoc::sim {

inline constexpr int32_t ENVOYS_PER_CIVIC = 1;

struct PlayerEnvoyComponent {
    int32_t available = 0; ///< Envoys earned and not yet sent
    int32_t lifetime  = 0; ///< Envoys earned over the whole game

    void grant(int32_t amount) {
        this->available += amount;
        this->lifetime += amount;
    }

    [[nodiscard]] bool spend(int32_t amount) {
        if (this->available < amount) {
            return false;
        }
        this->available -= amount;
        return true;
    }
};

} // namespace aoc::sim
