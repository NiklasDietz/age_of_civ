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

namespace aoc::map { class HexGrid; }

namespace aoc::sim {

/// ECS component attached to city entities.
struct CityComponent {
    PlayerId        owner;
    hex::AxialCoord location;
    std::string     name;
    int32_t         population = 1;
    float           foodSurplus = 0.0f;       ///< Accumulated food toward next pop
    float           productionProgress = 0.0f; ///< Accumulated production
    float           cultureBorderProgress = 0.0f; ///< Accumulated culture toward next tile
    int32_t         tilesClaimedCount = 0;    ///< Number of tiles claimed via culture

    /// Specialist citizens (not working tiles, producing yields directly).
    /// Entertainer: +2 luxury amenity. Scientist: +3 science. Taxman: +3 gold.
    int32_t entertainers = 0;
    int32_t scientists   = 0;
    int32_t taxmen       = 0;

    /// Tiles worked by this city's citizens.
    /// The city center is always in this list and doesn't consume a citizen.
    std::vector<hex::AxialCoord> workedTiles;

    /// Tiles locked by the player (won't be reassigned by auto-placement).
    std::vector<hex::AxialCoord> lockedTiles;

    /// Check if a tile is locked.
    [[nodiscard]] bool isTileLocked(hex::AxialCoord tile) const {
        for (const hex::AxialCoord& locked : this->lockedTiles) {
            if (locked == tile) { return true; }
        }
        return false;
    }

    /// Toggle lock on a tile.
    void toggleTileLock(hex::AxialCoord tile) {
        for (std::size_t i = 0; i < this->lockedTiles.size(); ++i) {
            if (this->lockedTiles[i] == tile) {
                this->lockedTiles.erase(this->lockedTiles.begin() + static_cast<std::ptrdiff_t>(i));
                return;
            }
        }
        this->lockedTiles.push_back(tile);
    }

    /// Number of freely assignable citizens (population, since center is free).
    [[nodiscard]] int32_t availableCitizens() const {
        int32_t assigned = static_cast<int32_t>(this->workedTiles.size()) - 1;  // -1 for free center
        if (assigned < 0) { assigned = 0; }
        return this->population - assigned;
    }

    /// True if this city was the original capital of some player.
    bool isOriginalCapital = false;

    /// The player who originally founded this city.
    PlayerId originalOwner = INVALID_PLAYER;

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

/// Focus type for auto-worker placement.
enum class WorkerFocus : uint8_t {
    Balanced,     ///< Equal weight across all yields
    Food,         ///< Prioritize food tiles
    Production,   ///< Prioritize production tiles
    Gold,         ///< Prioritize gold tiles
    Science,      ///< Prioritize science tiles
};

/**
 * @brief Auto-assign workers to the best available tiles based on focus.
 *
 * Respects locked tiles (won't remove them). Fills up to population capacity
 * with the highest-scoring tiles. City center is always worked for free.
 *
 * @param city  City component (mutated: workedTiles updated).
 * @param grid  Hex grid for tile yields and ownership.
 * @param focus What yield to prioritize.
 */
void autoAssignWorkers(CityComponent& city, const aoc::map::HexGrid& grid,
                        WorkerFocus focus = WorkerFocus::Balanced);

} // namespace aoc::sim
