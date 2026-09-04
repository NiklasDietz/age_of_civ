/**
 * @file DistrictAdjacency.cpp
 * @brief District adjacency bonus computation, appeal, and city projects.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/city/DistrictAdjacency.hpp"
#include "aoc/simulation/city/CityLoyalty.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

// ============================================================================
// Adjacency bonuses
// ============================================================================

NeighborTerrainCounts countNeighborTerrain(const aoc::map::HexGrid& grid,
                                            hex::AxialCoord center) {
    NeighborTerrainCounts counts{};
    const std::array<hex::AxialCoord, 6> neighbors = hex::neighbors(center);
    for (const hex::AxialCoord& nbr : neighbors) {
        if (!grid.isValid(nbr)) { continue; }
        const int32_t nbrIdx = grid.toIndex(nbr);

        const aoc::map::TerrainType terrain = grid.terrain(nbrIdx);
        const aoc::map::FeatureType feature = grid.feature(nbrIdx);

        if (terrain == aoc::map::TerrainType::Mountain) { ++counts.mountains; }
        if (feature == aoc::map::FeatureType::Forest)   { ++counts.forests; }
        if (feature == aoc::map::FeatureType::Jungle)   { ++counts.rainforests; }
        if (feature == aoc::map::FeatureType::Hills)    { ++counts.hills; }
        if (grid.improvement(nbrIdx) == aoc::map::ImprovementType::Mine)   { ++counts.mines; }
        if (grid.improvement(nbrIdx) == aoc::map::ImprovementType::Quarry) { ++counts.quarries; }
        if (aoc::map::isWater(terrain) && grid.resource(nbrIdx).isValid()) {
            ++counts.coastalResources;
        }
        if (grid.naturalWonder(nbrIdx) != aoc::map::NaturalWonderType::None) {
            ++counts.wonders;
        }
    }
    return counts;
}

void DistrictIndex::build(const aoc::game::GameState& gameState) {
    this->m_byTile.clear();
    for (const std::unique_ptr<aoc::game::Player>& p : gameState.players()) {
        if (p == nullptr) { continue; }
        for (const std::unique_ptr<aoc::game::City>& city : p->cities()) {
            if (city == nullptr) { continue; }
            for (const CityDistrictsComponent::PlacedDistrict& pd : city->districts().districts) {
                TileDistricts& entry = this->m_byTile[DistrictIndex::key(pd.location)];
                ++entry.total;
                if (pd.type == DistrictType::Harbor)     { ++entry.harbor; }
                if (pd.type == DistrictType::Industrial) { ++entry.industrial; }
                if (pd.type == DistrictType::CityCenter) { ++entry.cityCenter; }
                if (pd.type == DistrictType::Campus)     { ++entry.campus; }
            }
        }
    }
}

DistrictIndex::TileDistricts DistrictIndex::at(hex::AxialCoord location) const {
    const std::unordered_map<int64_t, TileDistricts>::const_iterator it =
        this->m_byTile.find(DistrictIndex::key(location));
    if (it == this->m_byTile.end()) {
        return TileDistricts{};
    }
    return it->second;
}

AdjacencyBonus computeAdjacencyBonus(const aoc::map::HexGrid& grid,
                                      const DistrictIndex& districts,
                                      DistrictType districtType,
                                      int32_t tileIndex) {
    AdjacencyBonus bonus{};
    const hex::AxialCoord center = grid.toAxial(tileIndex);
    const NeighborTerrainCounts terrain = countNeighborTerrain(grid, center);

    // Adjacent districts, one hash lookup per neighbour. This used to re-walk
    // every district of every city of every player once per neighbour.
    int32_t adjDistricts           = 0;
    int32_t adjHarborDistricts     = 0;
    int32_t adjIndustrialDistricts = 0;
    int32_t adjCityCenters         = 0;
    int32_t adjCampusDistricts     = 0;
    const std::array<hex::AxialCoord, 6> neighbors = hex::neighbors(center);
    for (const hex::AxialCoord& nbr : neighbors) {
        if (!grid.isValid(nbr)) { continue; }
        const DistrictIndex::TileDistricts here = districts.at(nbr);
        adjDistricts           += here.total;
        adjHarborDistricts     += here.harbor;
        adjIndustrialDistricts += here.industrial;
        adjCityCenters         += here.cityCenter;
        adjCampusDistricts     += here.campus;
    }

    // River edges on the district's own tile
    const int32_t adjRiverEdges = __builtin_popcount(grid.riverEdges(tileIndex));

    // Apply bonuses based on district type (Civ 6 adjacency rules)
    switch (districtType) {
        case DistrictType::Campus:
            bonus.science += static_cast<float>(terrain.mountains) * 1.0f;
            bonus.science += static_cast<float>(terrain.rainforests) * 0.5f;
            bonus.science += static_cast<float>(terrain.wonders) * 2.0f;
            // Adjacent Campus districts cluster research (research-park effect).
            bonus.science += static_cast<float>(adjCampusDistricts) * 1.0f;
            break;

        case DistrictType::Commercial:
            bonus.gold += (adjRiverEdges > 0) ? 2.0f : 0.0f;
            bonus.gold += static_cast<float>(adjDistricts) * 0.5f;
            bonus.gold += static_cast<float>(adjHarborDistricts) * 2.0f;
            break;

        case DistrictType::Industrial:
            bonus.production += static_cast<float>(terrain.mines) * 1.0f;
            bonus.production += static_cast<float>(terrain.quarries) * 1.0f;
            bonus.production += static_cast<float>(adjDistricts) * 0.5f;
            bonus.production += static_cast<float>(adjIndustrialDistricts) * 1.0f;
            break;

        case DistrictType::Harbor:
            bonus.gold += static_cast<float>(terrain.coastalResources) * 2.0f;
            bonus.gold += static_cast<float>(adjDistricts) * 1.0f;
            bonus.gold += static_cast<float>(adjCityCenters) * 2.0f;
            break;

        case DistrictType::HolySite:
            bonus.faith += static_cast<float>(terrain.mountains) * 1.0f;
            bonus.faith += static_cast<float>(terrain.forests) * 0.5f;
            bonus.faith += static_cast<float>(terrain.wonders) * 2.0f;
            break;

        case DistrictType::Encampment:
            // Defense bonus from hills (not yield but tracked for combat)
            break;

        default:
            break;
    }

    return bonus;
}

AdjacencyBonus computeAdjacencyBonus(const aoc::map::HexGrid& grid,
                                      const aoc::game::GameState& gameState,
                                      DistrictType districtType,
                                      int32_t tileIndex) {
    DistrictIndex districts;
    districts.build(gameState);
    return computeAdjacencyBonus(grid, districts, districtType, tileIndex);
}

// ============================================================================
// City Projects
// ============================================================================

void completeCityProject(aoc::game::GameState& gameState,
                          aoc::game::City& city,
                          CityProjectType project) {
    switch (project) {
        case CityProjectType::BreadAndCircuses: {
            city.loyalty().loyalty = std::min(100.0f, city.loyalty().loyalty + 20.0f);
            LOG_INFO("City %s: Bread and Circuses completed (+20 loyalty)", city.name().c_str());
            break;
        }
        case CityProjectType::CampusResearch: {
            // Grant science burst to the player's current research
            aoc::game::Player* gsPlayer = gameState.player(city.owner());
            if (gsPlayer != nullptr) {
                gsPlayer->tech().researchProgress += 50.0f;
            }
            LOG_INFO("City %s: Campus Research Grant completed (+50 science)", city.name().c_str());
            break;
        }
        case CityProjectType::IndustrialSurge: {
            // Boost current production queue. Completion bool is intentionally
            // discarded -- the +50 burst is a one-shot boost; if it pushes the
            // item over its cost threshold, the city's normal production tick
            // will pop it on the next turn.
            ProductionQueueComponent& queue = city.production();
            if (!queue.isEmpty()) {
                [[maybe_unused]] const bool completed = queue.addProgress(50.0f);
            }
            LOG_INFO("City %s: Industrial Surge completed (+50 production)", city.name().c_str());
            break;
        }
        case CityProjectType::CommercialInvestment: {
            // Grant gold burst to treasury
            aoc::game::Player* gsPlayer = gameState.player(city.owner());
            if (gsPlayer != nullptr) {
                gsPlayer->monetary().treasury += 100;
            }
            LOG_INFO("City %s: Commercial Investment completed (+100 gold)", city.name().c_str());
            break;
        }
        case CityProjectType::ShipyardRush:
            LOG_INFO("City %s: Shipyard Rush completed (next naval unit -50%% cost)", city.name().c_str());
            break;
        case CityProjectType::MilitaryTraining:
            LOG_INFO("City %s: Military Training completed (+XP for trained units)", city.name().c_str());
            break;
        default:
            break;
    }
}

} // namespace aoc::sim
