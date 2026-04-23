/**
 * @file City.cpp
 * @brief Per-city game state implementation.
 */

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

#include <algorithm>
#include <limits>

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
                              aoc::sim::WorkerFocus focus,
                              const Player* owner) {
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

    // Score all tiles this city is allowed to work. No fixed radius: every
    // tile that the owning player controls is a candidate, filtered by
    // (a) per-tile manual override on the Player, or (b) nearest-city rule
    // among the player's cities. A hard sanity cap of 12 hexes keeps runaway
    // yield hunts local when cities are sparsely placed.
    constexpr int32_t SANITY_CAP = 12;

    struct TileScore {
        aoc::hex::AxialCoord coord;
        float score;
    };
    std::vector<TileScore> candidates;

    const int32_t totalTiles = grid.tileCount();
    for (int32_t idx = 0; idx < totalTiles; ++idx) {
        if (grid.owner(idx) != this->m_owner) {
            continue;
        }
        if (grid.movementCost(idx) == 0) {
            continue;
        }
        const aoc::hex::AxialCoord tile = grid.toAxial(idx);
        if (tile == this->m_location) {
            continue;
        }
        if (grid.distance(tile, this->m_location) > SANITY_CAP) {
            continue;
        }

        // City-assignment filter. If an override exists, only the matching
        // city may work the tile. Otherwise nearest-city-of-owner wins.
        if (owner != nullptr) {
            const aoc::hex::AxialCoord* override = owner->tileCityOverride(idx);
            if (override != nullptr) {
                if (*override != this->m_location) { continue; }
            } else {
                int32_t bestDist = std::numeric_limits<int32_t>::max();
                aoc::hex::AxialCoord bestLoc{};
                for (const std::unique_ptr<City>& other : owner->cities()) {
                    if (other == nullptr) { continue; }
                    const int32_t d = grid.distance(tile, other->location());
                    if (d < bestDist) {
                        bestDist = d;
                        bestLoc = other->location();
                    }
                }
                if (bestLoc != this->m_location) { continue; }
            }
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
        // Bonus for tiles with resources.  Strategic resources (Oil, Coal,
        // Iron, etc.) get a much larger kicker than generic luxury/bonus
        // resources because their downstream production chain is worth far
        // more than their raw yield suggests.  Previously a flat +2.0 meant
        // a Grassland tile with 2 food beat an Oil tile with 0 food + 2
        // resource bonus, so cities never worked strategic tiles and the
        // production chain starved even after tech reveals.
        const aoc::ResourceId resId = grid.resource(idx);
        if (resId.isValid() && resId.value < aoc::sim::goodCount()) {
            const aoc::sim::GoodDef& gd = aoc::sim::goodDef(resId.value);
            if (gd.category == aoc::sim::GoodCategory::RawStrategic) {
                score += 8.0f;  // Strategic: oil/coal/iron/etc — chain-feeding
            } else {
                score += 2.0f;  // Generic resource bonus
            }
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
