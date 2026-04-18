/**
 * @file City.cpp
 * @brief Per-city game state implementation.
 */

#include "aoc/game/City.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"

#include <algorithm>
#include <iterator>

namespace aoc::game {

City::City(PlayerId owner, aoc::hex::AxialCoord location, const std::string& name)
    : m_owner(owner)
    , m_location(location)
    , m_name(name)
    , m_originalOwner(owner)
{
    // The city center tile is always worked and free (does not consume a citizen)
    this->m_workedTiles.push_back(location);

    // Every city starts with a CityCenter district at its location
    aoc::sim::CityDistrictsComponent::PlacedDistrict center;
    center.type = aoc::sim::DistrictType::CityCenter;
    center.location = location;
    this->m_districts.districts.push_back(std::move(center));
}

int32_t City::availableCitizens() const {
    // Center tile is free, so subtract 1 from worked tile count
    int32_t assigned = static_cast<int32_t>(this->m_workedTiles.size()) - 1;
    int32_t available = this->m_population - assigned;
    return (available > 0) ? available : 0;
}

bool City::isTileWorked(aoc::hex::AxialCoord tile) const {
    for (const aoc::hex::AxialCoord& worked : this->m_workedTiles) {
        if (worked == tile) {
            return true;
        }
    }
    return false;
}

void City::assignWorker(aoc::hex::AxialCoord tile) {
    if (!this->isTileWorked(tile)) {
        this->m_workedTiles.push_back(tile);
    }
}

void City::removeWorker(aoc::hex::AxialCoord tile) {
    // Never remove the city center tile
    if (tile == this->m_location) {
        return;
    }
    std::vector<aoc::hex::AxialCoord>::iterator it = std::find(
        this->m_workedTiles.begin(), this->m_workedTiles.end(), tile);
    if (it != this->m_workedTiles.end()) {
        this->m_workedTiles.erase(it);
    }
}

void City::toggleWorker(aoc::hex::AxialCoord tile) {
    if (this->isTileWorked(tile)) {
        this->removeWorker(tile);
    } else {
        this->assignWorker(tile);
    }
}

bool City::isTileLocked(aoc::hex::AxialCoord tile) const {
    for (const aoc::hex::AxialCoord& locked : this->m_lockedTiles) {
        if (locked == tile) {
            return true;
        }
    }
    return false;
}

void City::toggleTileLock(aoc::hex::AxialCoord tile) {
    for (std::size_t i = 0; i < this->m_lockedTiles.size(); ++i) {
        if (this->m_lockedTiles[i] == tile) {
            this->m_lockedTiles.erase(this->m_lockedTiles.begin()
                + static_cast<std::ptrdiff_t>(i));
            return;
        }
    }
    this->m_lockedTiles.push_back(tile);
}

void City::autoAssignWorkers(const aoc::map::HexGrid& grid,
                              aoc::sim::WorkerFocus focus) {
    // Keep locked tiles and the city center, remove everything else
    std::vector<aoc::hex::AxialCoord> kept;
    kept.push_back(this->m_location);  // Center always worked (free)
    for (const aoc::hex::AxialCoord& tile : this->m_workedTiles) {
        if (tile == this->m_location) {
            continue;
        }
        if (this->isTileLocked(tile)) {
            kept.push_back(tile);
        }
    }
    this->m_workedTiles = kept;

    // How many citizens can still be assigned
    int32_t slotsAvailable = this->m_population
        - (static_cast<int32_t>(this->m_workedTiles.size()) - 1);
    if (slotsAvailable <= 0) {
        return;
    }

    // Score all owned tiles within 3 hexes
    struct TileScore {
        aoc::hex::AxialCoord coord;
        float score;
    };
    std::vector<TileScore> candidates;

    std::vector<aoc::hex::AxialCoord> nearby;
    nearby.reserve(40);
    aoc::hex::spiral(this->m_location, 3, std::back_inserter(nearby));

    for (const aoc::hex::AxialCoord& tile : nearby) {
        if (!grid.isValid(tile)) {
            continue;
        }
        int32_t idx = grid.toIndex(tile);
        if (grid.owner(idx) != this->m_owner) {
            continue;
        }
        if (grid.movementCost(idx) == 0) {
            continue;
        }
        if (tile == this->m_location) {
            continue;
        }

        // Skip already worked tiles
        bool alreadyWorked = false;
        for (const aoc::hex::AxialCoord& worked : this->m_workedTiles) {
            if (worked == tile) {
                alreadyWorked = true;
                break;
            }
        }
        if (alreadyWorked) {
            continue;
        }

        aoc::map::TileYield yields = grid.tileYield(idx);
        float score = 0.0f;
        switch (focus) {
            case aoc::sim::WorkerFocus::Food:
                score = static_cast<float>(yields.food) * 3.0f
                      + static_cast<float>(yields.production) * 1.0f
                      + static_cast<float>(yields.gold) * 0.5f
                      + static_cast<float>(yields.science) * 0.5f;
                break;
            case aoc::sim::WorkerFocus::Production:
                score = static_cast<float>(yields.food) * 1.0f
                      + static_cast<float>(yields.production) * 3.0f
                      + static_cast<float>(yields.gold) * 0.5f
                      + static_cast<float>(yields.science) * 0.5f;
                break;
            case aoc::sim::WorkerFocus::Gold:
                score = static_cast<float>(yields.food) * 0.5f
                      + static_cast<float>(yields.production) * 0.5f
                      + static_cast<float>(yields.gold) * 3.0f
                      + static_cast<float>(yields.science) * 1.0f;
                break;
            case aoc::sim::WorkerFocus::Science:
                score = static_cast<float>(yields.food) * 0.5f
                      + static_cast<float>(yields.production) * 1.0f
                      + static_cast<float>(yields.gold) * 0.5f
                      + static_cast<float>(yields.science) * 3.0f;
                break;
            case aoc::sim::WorkerFocus::Balanced:
            default:
                score = static_cast<float>(yields.food) * 2.0f
                      + static_cast<float>(yields.production) * 1.5f
                      + static_cast<float>(yields.gold) * 1.0f
                      + static_cast<float>(yields.science) * 1.0f;
                break;
        }
        // Bonus for tiles with resources
        if (grid.resource(idx).isValid()) {
            score += 2.0f;
        }

        candidates.push_back({tile, score});
    }

    // Sort by score descending
    std::sort(candidates.begin(), candidates.end(),
        [](const TileScore& a, const TileScore& b) { return a.score > b.score; });

    // Assign top-scoring tiles
    for (const TileScore& ts : candidates) {
        if (slotsAvailable <= 0) {
            break;
        }
        this->m_workedTiles.push_back(ts.coord);
        --slotsAvailable;
    }
}

} // namespace aoc::game
