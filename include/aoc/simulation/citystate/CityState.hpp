#pragma once

/// @file CityState.hpp
/// @brief City-state definitions, ECS components, and per-turn processing.

#include "aoc/core/Types.hpp"
#include "aoc/core/Random.hpp"
#include "aoc/map/HexCoord.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace aoc::ecs {
class World;
}

namespace aoc::map {
class HexGrid;
}

namespace aoc::sim {

/// City-state specialization type.
enum class CityStateType : uint8_t {
    Militaristic,
    Scientific,
    Cultural,
    Trade,
    Religious,
    Industrial,
    Count
};

/// Static definition for a city-state.
struct CityStateDef {
    uint8_t        id;
    std::string_view name;
    CityStateType  type;
};

/// Total number of defined city-states.
inline constexpr uint8_t CITY_STATE_COUNT = 8;

/// All city-state definitions.
inline constexpr std::array<CityStateDef, CITY_STATE_COUNT> CITY_STATE_DEFS = {{
    {0, "Kabul",     CityStateType::Militaristic},
    {1, "Valletta",  CityStateType::Militaristic},
    {2, "Geneva",    CityStateType::Scientific},
    {3, "Seoul",     CityStateType::Scientific},
    {4, "Kumasi",    CityStateType::Cultural},
    {5, "Zanzibar",  CityStateType::Cultural},
    {6, "Lisbon",    CityStateType::Trade},
    {7, "Singapore", CityStateType::Trade},
}};

/// Special player ID range for city-states (200-207).
inline constexpr PlayerId CITY_STATE_PLAYER_BASE = 200;

/// ECS component for a city-state entity.
struct CityStateComponent {
    uint8_t       defId;
    CityStateType type;
    hex::AxialCoord location;
    std::array<int8_t, MAX_PLAYERS> envoys{}; ///< Envoy count per player
    PlayerId      suzerain = INVALID_PLAYER;

    /// Get the player with the most envoys (suzerain).
    [[nodiscard]] PlayerId computeSuzerain() const {
        PlayerId best = INVALID_PLAYER;
        int8_t bestCount = 0;
        for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
            if (this->envoys[i] > bestCount) {
                bestCount = this->envoys[i];
                best = static_cast<PlayerId>(i);
            }
        }
        // Need at least 3 envoys to be suzerain
        if (bestCount < 3) {
            return INVALID_PLAYER;
        }
        return best;
    }
};

/// Spawn city-states during map generation.
void spawnCityStates(aoc::ecs::World& world, aoc::map::HexGrid& grid,
                      int32_t count, aoc::Random& rng);

/// Process city-state bonuses each turn for a given player.
void processCityStateBonuses(aoc::ecs::World& world, PlayerId player);

} // namespace aoc::sim
