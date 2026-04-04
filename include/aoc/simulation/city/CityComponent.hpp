#pragma once

/**
 * @file CityComponent.hpp
 * @brief ECS component for cities.
 */

#include "aoc/map/HexCoord.hpp"
#include "aoc/core/Types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace aoc::sim {

/// ECS component attached to city entities.
struct CityComponent {
    PlayerId        owner;
    hex::AxialCoord location;
    std::string     name;
    int32_t         population = 1;
    float           foodSurplus = 0.0f;       ///< Accumulated food toward next pop
    float           productionProgress = 0.0f; ///< Accumulated production

    /// Tiles worked by this city's citizens.
    std::vector<hex::AxialCoord> workedTiles;

    /// Create a new city with initial values.
    static CityComponent create(PlayerId owner, hex::AxialCoord location,
                                 std::string name) {
        CityComponent city{};
        city.owner      = owner;
        city.location   = location;
        city.name       = std::move(name);
        city.population = 1;
        // Auto-work the city center tile
        city.workedTiles.push_back(location);
        return city;
    }
};

} // namespace aoc::sim
