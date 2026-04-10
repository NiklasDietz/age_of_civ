#pragma once

/**
 * @file PlayerCityIndex.hpp
 * @brief Index of cities grouped by owning player for O(1) access.
 *
 * The second most common pattern after single-component player lookup is
 * iterating all cities belonging to a player:
 * @code
 *   for (uint32_t i = 0; i < cityPool->size(); ++i) {
 *       if (cityPool->data()[i].owner == player) { ... }
 *   }
 * @endcode
 *
 * PlayerCityIndex provides direct access to a player's city list.
 */

#include "aoc/ecs/World.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/core/Types.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace aoc::ecs {

inline constexpr int32_t MAX_CITIES_PER_PLAYER = 32;

struct PlayerCityEntry {
    EntityId entity = NULL_ENTITY;
    int32_t  poolIndex = -1;  ///< Index into CityComponent pool for direct data access
};

class PlayerCityIndex {
public:
    /// Rebuild from world state.
    void rebuild(World& world) {
        for (int32_t p = 0; p < 16; ++p) {
            this->m_counts[p] = 0;
        }

        ComponentPool<aoc::sim::CityComponent>* pool = world.getPool<aoc::sim::CityComponent>();
        if (pool == nullptr) {
            return;
        }

        for (uint32_t i = 0; i < pool->size(); ++i) {
            PlayerId owner = pool->data()[i].owner;
            if (owner < 16 && this->m_counts[owner] < MAX_CITIES_PER_PLAYER) {
                int32_t idx = this->m_counts[owner];
                this->m_cities[owner][idx].entity = pool->entities()[i];
                this->m_cities[owner][idx].poolIndex = static_cast<int32_t>(i);
                ++this->m_counts[owner];
            }
        }
    }

    /// Get city entries for a player.
    [[nodiscard]] const PlayerCityEntry* cities(PlayerId player) const {
        if (player >= 16) { return nullptr; }
        return this->m_cities[player].data();
    }

    /// Number of cities for a player.
    [[nodiscard]] int32_t cityCount(PlayerId player) const {
        if (player >= 16) { return 0; }
        return this->m_counts[player];
    }

private:
    std::array<std::array<PlayerCityEntry, MAX_CITIES_PER_PLAYER>, 16> m_cities = {};
    std::array<int32_t, 16> m_counts = {};
};

} // namespace aoc::ecs
