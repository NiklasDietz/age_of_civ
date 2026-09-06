/**
 * @file DistrictPlacement.cpp
 * @brief District tile rules, scoring and placement (see the header).
 */

#include "aoc/simulation/city/DistrictPlacement.hpp"

#include "aoc/core/Log.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/DistrictAdjacency.hpp"

#include <memory>
#include <vector>

namespace aoc::sim {

namespace {

/// Adjacency counts for one yield unit; the tile's own yields are worth less
/// than an adjacency point, so a strong site wins over a fertile one.
constexpr float ADJACENCY_WEIGHT = 10.0f;

[[nodiscard]] bool districtOnTile(const aoc::game::Player& player, hex::AxialCoord at) {
    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        if (city == nullptr) {
            continue;
        }
        for (const CityDistrictsComponent::PlacedDistrict& district : city->districts().districts) {
            if (district.location == at) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

std::string_view districtTileReasonName(DistrictTileReason reason) {
    switch (reason) {
        case DistrictTileReason::Ok:         return "Ok";
        case DistrictTileReason::OffMap:     return "Off the map";
        case DistrictTileReason::NotOwned:   return "Not your territory";
        case DistrictTileReason::TooFar:     return "Too far from the city";
        case DistrictTileReason::CityCenter: return "The city centre";
        case DistrictTileReason::Occupied:   return "A district stands here";
        case DistrictTileReason::NeedsWater: return "A Harbor needs coastal water";
        case DistrictTileReason::NeedsLand:  return "Districts need land";
        case DistrictTileReason::Impassable: return "Nothing can build here";
        default:                             return "Unknown";
    }
}

bool tileHasDistrict(const aoc::game::GameState& gameState, hex::AxialCoord at) {
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        if (player != nullptr && districtOnTile(*player, at)) {
            return true;
        }
    }
    for (const std::unique_ptr<aoc::game::Player>& seat : gameState.cityStatePlayers()) {
        if (seat != nullptr && districtOnTile(*seat, at)) {
            return true;
        }
    }
    return false;
}

DistrictTileReason districtTileReason(const aoc::game::GameState& gameState, const aoc::map::HexGrid& grid,
                                      const aoc::game::City& city, DistrictType type, hex::AxialCoord at) {
    if (!grid.isValid(at)) {
        return DistrictTileReason::OffMap;
    }
    if (at == city.location()) {
        return DistrictTileReason::CityCenter;
    }
    const int32_t index = grid.toIndex(at);
    if (grid.owner(index) != city.owner()) {
        return DistrictTileReason::NotOwned;
    }
    if (grid.distance(city.location(), at) > CITY_WORK_RADIUS) {
        return DistrictTileReason::TooFar;
    }
    if (tileHasDistrict(gameState, at)) {
        return DistrictTileReason::Occupied;
    }
    const bool water = aoc::map::isWater(grid.terrain(index));
    if (type == DistrictType::Harbor) {
        // A Harbor sits in the water beside the city, like a real port.
        if (!water) {
            return DistrictTileReason::NeedsWater;
        }
        if (grid.distance(city.location(), at) > 1) {
            return DistrictTileReason::NeedsWater;
        }
        return DistrictTileReason::Ok;
    }
    if (water) {
        return DistrictTileReason::NeedsLand;
    }
    if (aoc::map::isImpassable(grid.terrain(index))) {
        return DistrictTileReason::Impassable;
    }
    return DistrictTileReason::Ok;
}

std::vector<hex::AxialCoord> districtCandidateTiles(const aoc::game::GameState& gameState,
                                                    const aoc::map::HexGrid& grid,
                                                    const aoc::game::City& city, DistrictType type) {
    std::vector<hex::AxialCoord> tiles;
    std::vector<hex::AxialCoord> nearby;
    nearby.reserve(64);
    hex::spiral(city.location(), CITY_WORK_RADIUS, std::back_inserter(nearby));
    for (const hex::AxialCoord& tile : nearby) {
        if (districtTileReason(gameState, grid, city, type, tile) == DistrictTileReason::Ok) {
            tiles.push_back(tile);
        }
    }
    return tiles;
}

float districtTileScore(const aoc::game::GameState& gameState, const aoc::map::HexGrid& grid,
                        DistrictType type, hex::AxialCoord at) {
    if (!grid.isValid(at)) {
        return 0.0f;
    }
    const int32_t index    = grid.toIndex(at);
    const AdjacencyBonus a = computeAdjacencyBonus(grid, gameState, type, index);
    const float adjacency  = a.food + a.production + a.gold + a.science + a.culture + a.faith;
    const aoc::map::TileYield yield = grid.tileYield(index);
    const float lost = static_cast<float>(yield.food) + static_cast<float>(yield.production)
                     + static_cast<float>(yield.gold);
    return adjacency * ADJACENCY_WEIGHT - lost;
}

hex::AxialCoord bestDistrictTile(const aoc::game::GameState& gameState, const aoc::map::HexGrid& grid,
                                 const aoc::game::City& city, DistrictType type) {
    const std::vector<hex::AxialCoord> candidates = districtCandidateTiles(gameState, grid, city, type);
    hex::AxialCoord best = city.location();
    bool found           = false;
    float bestScore      = 0.0f;
    for (const hex::AxialCoord& tile : candidates) {
        const float score = districtTileScore(gameState, grid, type, tile);
        // Strictly greater, so the first candidate of a tie wins; the candidate
        // list is a spiral around the centre, so the choice is the same in
        // every run.
        if (!found || score > bestScore) {
            found     = true;
            best      = tile;
            bestScore = score;
        }
    }
    return best;
}

CityDistrictsComponent::PlacedDistrict& placeDistrictOnTile(aoc::map::HexGrid& grid, aoc::game::City& city,
                                                            DistrictType type, hex::AxialCoord at) {
    CityDistrictsComponent::PlacedDistrict placed;
    placed.type     = type;
    placed.location = at;
    city.districts().districts.push_back(std::move(placed));

    if (grid.isValid(at) && at != city.location()) {
        const int32_t index = grid.toIndex(at);
        if (grid.improvement(index) != aoc::map::ImprovementType::None) {
            grid.setImprovement(index, aoc::map::ImprovementType::None);
        }
        if (grid.isPillaged(index)) {
            grid.setPillaged(index, false);
        }
        city.removeWorker(at); // the district covers the tile; nobody farms it
    }
    return city.districts().districts.back();
}

} // namespace aoc::sim
