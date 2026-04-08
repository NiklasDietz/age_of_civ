/**
 * @file DistrictAdjacency.cpp
 * @brief District adjacency bonus computation, appeal, and city projects.
 */

#include "aoc/simulation/city/DistrictAdjacency.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/CityLoyalty.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

// ============================================================================
// Adjacency bonuses
// ============================================================================

AdjacencyBonus computeAdjacencyBonus(const aoc::map::HexGrid& grid,
                                      const aoc::ecs::World& world,
                                      DistrictType districtType,
                                      int32_t tileIndex) {
    AdjacencyBonus bonus{};
    hex::AxialCoord center = grid.toAxial(tileIndex);
    std::array<hex::AxialCoord, 6> neighbors = hex::neighbors(center);

    // Count adjacent features
    int32_t adjMountains = 0;
    int32_t adjForests = 0;
    int32_t adjRainforests = 0;
    int32_t adjDistricts = 0;
    int32_t adjRiverEdges = 0;
    int32_t adjCoastalResources = 0;
    int32_t adjMines = 0;
    int32_t adjHills = 0;
    int32_t adjWonders = 0;

    for (const hex::AxialCoord& nbr : neighbors) {
        if (!grid.isValid(nbr)) { continue; }
        int32_t nbrIdx = grid.toIndex(nbr);

        aoc::map::TerrainType terrain = grid.terrain(nbrIdx);
        aoc::map::FeatureType feature = grid.feature(nbrIdx);

        if (terrain == aoc::map::TerrainType::Mountain) { ++adjMountains; }
        if (feature == aoc::map::FeatureType::Forest)   { ++adjForests; }
        if (feature == aoc::map::FeatureType::Jungle)   { ++adjRainforests; }
        if (feature == aoc::map::FeatureType::Hills)    { ++adjHills; }
        if (grid.improvement(nbrIdx) == aoc::map::ImprovementType::Mine) { ++adjMines; }

        // Check for adjacent districts (any city's district on this tile)
        const aoc::ecs::ComponentPool<CityDistrictsComponent>* distPool =
            world.getPool<CityDistrictsComponent>();
        if (distPool != nullptr) {
            for (uint32_t d = 0; d < distPool->size(); ++d) {
                for (const CityDistrictsComponent::PlacedDistrict& pd : distPool->data()[d].districts) {
                    if (pd.location == nbr) {
                        ++adjDistricts;
                    }
                }
            }
        }

        // Coastal resources
        if (aoc::map::isWater(terrain) && grid.resource(nbrIdx).isValid()) {
            ++adjCoastalResources;
        }

        // Natural wonders
        if (grid.naturalWonder(nbrIdx) != aoc::map::NaturalWonderType::None) {
            ++adjWonders;
        }
    }

    // River edges on the district's own tile
    adjRiverEdges = __builtin_popcount(grid.riverEdges(tileIndex));

    // Apply bonuses based on district type
    switch (districtType) {
        case DistrictType::Campus:
            bonus.science += static_cast<float>(adjMountains) * 1.0f;
            bonus.science += static_cast<float>(adjRainforests) * 0.5f;
            bonus.science += static_cast<float>(adjWonders) * 1.0f;
            break;

        case DistrictType::Commercial:
            bonus.gold += (adjRiverEdges > 0) ? 2.0f : 0.0f;
            bonus.gold += static_cast<float>(adjDistricts) * 0.5f;
            break;

        case DistrictType::Industrial:
            bonus.production += static_cast<float>(adjMines) * 1.0f;
            bonus.production += static_cast<float>(adjDistricts) * 0.5f;
            break;

        case DistrictType::Harbor:
            bonus.gold += static_cast<float>(adjCoastalResources) * 2.0f;
            bonus.gold += static_cast<float>(adjDistricts) * 1.0f;
            break;

        case DistrictType::HolySite:
            bonus.faith += static_cast<float>(adjMountains) * 1.0f;
            bonus.faith += static_cast<float>(adjForests) * 0.5f;
            bonus.faith += static_cast<float>(adjWonders) * 1.0f;
            break;

        case DistrictType::Encampment:
            // No yield adjacency, but defense bonus
            break;

        default:
            break;
    }

    return bonus;
}

// ============================================================================
// Appeal
// ============================================================================

int32_t computeTileAppeal(const aoc::map::HexGrid& grid,
                          const aoc::ecs::World& world,
                          int32_t tileIndex) {
    int32_t appeal = 0;
    hex::AxialCoord center = grid.toAxial(tileIndex);
    std::array<hex::AxialCoord, 6> neighbors = hex::neighbors(center);

    // Own tile features
    aoc::map::FeatureType ownFeature = grid.feature(tileIndex);
    if (ownFeature == aoc::map::FeatureType::Forest) { appeal += 1; }
    if (ownFeature == aoc::map::FeatureType::Jungle) { appeal -= 1; }
    if (ownFeature == aoc::map::FeatureType::Marsh)  { appeal -= 1; }
    if (ownFeature == aoc::map::FeatureType::Oasis)  { appeal += 2; }

    if (grid.naturalWonder(tileIndex) != aoc::map::NaturalWonderType::None) { appeal += 4; }

    for (const hex::AxialCoord& nbr : neighbors) {
        if (!grid.isValid(nbr)) { continue; }
        int32_t nbrIdx = grid.toIndex(nbr);

        aoc::map::TerrainType terrain = grid.terrain(nbrIdx);
        if (terrain == aoc::map::TerrainType::Coast) { appeal += 1; }
        if (terrain == aoc::map::TerrainType::Mountain) { appeal += 1; }

        if (grid.naturalWonder(nbrIdx) != aoc::map::NaturalWonderType::None) { appeal += 2; }

        // Improvements
        aoc::map::ImprovementType imp = grid.improvement(nbrIdx);
        if (imp == aoc::map::ImprovementType::Mine) { appeal -= 1; }

        // Adjacent districts
        const aoc::ecs::ComponentPool<CityDistrictsComponent>* distPool =
            world.getPool<CityDistrictsComponent>();
        if (distPool != nullptr) {
            for (uint32_t d = 0; d < distPool->size(); ++d) {
                for (const CityDistrictsComponent::PlacedDistrict& pd : distPool->data()[d].districts) {
                    if (pd.location == nbr) {
                        if (pd.type == DistrictType::Industrial) { appeal -= 1; }
                        if (pd.type == DistrictType::Encampment) { appeal -= 1; }
                        if (pd.type == DistrictType::HolySite)   { appeal += 1; }
                    }
                }
            }
        }
    }

    return appeal;
}

// ============================================================================
// City Projects
// ============================================================================

void completeCityProject(aoc::ecs::World& world, EntityId cityEntity,
                         CityProjectType project) {
    CityComponent* city = world.tryGetComponent<CityComponent>(cityEntity);
    if (city == nullptr) { return; }

    switch (project) {
        case CityProjectType::BreadAndCircuses: {
            CityLoyaltyComponent* loyalty =
                world.tryGetComponent<CityLoyaltyComponent>(cityEntity);
            if (loyalty != nullptr) {
                loyalty->loyalty = std::min(100.0f, loyalty->loyalty + 20.0f);
            }
            LOG_INFO("City %s: Bread and Circuses completed (+20 loyalty)", city->name.c_str());
            break;
        }
        case CityProjectType::CampusResearch: {
            // Grant science burst to the player's current research
            aoc::ecs::ComponentPool<PlayerTechComponent>* techPool =
                world.getPool<PlayerTechComponent>();
            if (techPool != nullptr) {
                for (uint32_t i = 0; i < techPool->size(); ++i) {
                    if (techPool->data()[i].owner == city->owner) {
                        techPool->data()[i].researchProgress += 50.0f;
                        break;
                    }
                }
            }
            LOG_INFO("City %s: Campus Research Grant completed (+50 science)", city->name.c_str());
            break;
        }
        case CityProjectType::IndustrialSurge: {
            // Boost current production queue
            ProductionQueueComponent* queue =
                world.tryGetComponent<ProductionQueueComponent>(cityEntity);
            if (queue != nullptr && !queue->isEmpty()) {
                queue->addProgress(50.0f);
            }
            LOG_INFO("City %s: Industrial Surge completed (+50 production)", city->name.c_str());
            break;
        }
        case CityProjectType::CommercialInvestment: {
            // Grant gold burst to treasury
            aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
                world.getPool<MonetaryStateComponent>();
            if (monetaryPool != nullptr) {
                for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
                    if (monetaryPool->data()[i].owner == city->owner) {
                        monetaryPool->data()[i].treasury += 100;
                        break;
                    }
                }
            }
            LOG_INFO("City %s: Commercial Investment completed (+100 gold)", city->name.c_str());
            break;
        }
        case CityProjectType::ShipyardRush:
            LOG_INFO("City %s: Shipyard Rush completed (next naval unit -50%% cost)", city->name.c_str());
            break;
        case CityProjectType::MilitaryTraining:
            LOG_INFO("City %s: Military Training completed (+XP for trained units)", city->name.c_str());
            break;
        default:
            break;
    }
}

} // namespace aoc::sim
