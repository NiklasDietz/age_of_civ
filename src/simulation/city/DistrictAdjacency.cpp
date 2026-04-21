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

AdjacencyBonus computeAdjacencyBonus(const aoc::map::HexGrid& grid,
                                      const aoc::game::GameState& gameState,
                                      DistrictType districtType,
                                      int32_t tileIndex) {
    AdjacencyBonus bonus{};
    hex::AxialCoord center = grid.toAxial(tileIndex);
    std::array<hex::AxialCoord, 6> neighbors = hex::neighbors(center);

    // Count adjacent features and district types
    int32_t adjMountains = 0;
    int32_t adjForests = 0;
    int32_t adjRainforests = 0;
    int32_t adjDistricts = 0;
    int32_t adjRiverEdges = 0;
    int32_t adjCoastalResources = 0;
    int32_t adjMines = 0;
    int32_t adjQuarries = 0;
    int32_t adjHills = 0;
    int32_t adjWonders = 0;
    int32_t adjHarborDistricts = 0;
    int32_t adjIndustrialDistricts = 0;
    int32_t adjCityCenters = 0;
    int32_t adjCampusDistricts = 0;

    (void)adjHills;

    for (const hex::AxialCoord& nbr : neighbors) {
        if (!grid.isValid(nbr)) { continue; }
        const int32_t nbrIdx = grid.toIndex(nbr);

        aoc::map::TerrainType terrain = grid.terrain(nbrIdx);
        aoc::map::FeatureType feature = grid.feature(nbrIdx);

        if (terrain == aoc::map::TerrainType::Mountain) { ++adjMountains; }
        if (feature == aoc::map::FeatureType::Forest)   { ++adjForests; }
        if (feature == aoc::map::FeatureType::Jungle)   { ++adjRainforests; }
        if (feature == aoc::map::FeatureType::Hills)    { ++adjHills; }
        if (grid.improvement(nbrIdx) == aoc::map::ImprovementType::Mine)   { ++adjMines; }
        if (grid.improvement(nbrIdx) == aoc::map::ImprovementType::Quarry) { ++adjQuarries; }

        // Check for adjacent districts across all cities of all players
        for (const std::unique_ptr<aoc::game::Player>& p : gameState.players()) {
            for (const std::unique_ptr<aoc::game::City>& city : p->cities()) {
                for (const CityDistrictsComponent::PlacedDistrict& pd : city->districts().districts) {
                    if (pd.location == nbr) {
                        ++adjDistricts;
                        if (pd.type == DistrictType::Harbor)     { ++adjHarborDistricts; }
                        if (pd.type == DistrictType::Industrial) { ++adjIndustrialDistricts; }
                        if (pd.type == DistrictType::CityCenter) { ++adjCityCenters; }
                        if (pd.type == DistrictType::Campus)     { ++adjCampusDistricts; }
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

    // Apply bonuses based on district type (Civ 6 adjacency rules)
    switch (districtType) {
        case DistrictType::Campus:
            bonus.science += static_cast<float>(adjMountains) * 1.0f;
            bonus.science += static_cast<float>(adjRainforests) * 0.5f;
            bonus.science += static_cast<float>(adjWonders) * 2.0f;
            // Adjacent Campus districts cluster research (research-park effect).
            // Previously counted but never applied — the bonus was dead code.
            bonus.science += static_cast<float>(adjCampusDistricts) * 1.0f;
            break;

        case DistrictType::Commercial:
            bonus.gold += (adjRiverEdges > 0) ? 2.0f : 0.0f;
            bonus.gold += static_cast<float>(adjDistricts) * 0.5f;
            bonus.gold += static_cast<float>(adjHarborDistricts) * 2.0f;
            break;

        case DistrictType::Industrial:
            bonus.production += static_cast<float>(adjMines) * 1.0f;
            bonus.production += static_cast<float>(adjQuarries) * 1.0f;
            bonus.production += static_cast<float>(adjDistricts) * 0.5f;
            bonus.production += static_cast<float>(adjIndustrialDistricts) * 1.0f;
            break;

        case DistrictType::Harbor:
            bonus.gold += static_cast<float>(adjCoastalResources) * 2.0f;
            bonus.gold += static_cast<float>(adjDistricts) * 1.0f;
            bonus.gold += static_cast<float>(adjCityCenters) * 2.0f;
            break;

        case DistrictType::HolySite:
            bonus.faith += static_cast<float>(adjMountains) * 1.0f;
            bonus.faith += static_cast<float>(adjForests) * 0.5f;
            bonus.faith += static_cast<float>(adjWonders) * 2.0f;
            break;

        case DistrictType::Encampment:
            // Defense bonus from hills (not yield but tracked for combat)
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
                          const aoc::game::GameState& gameState,
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
        const int32_t nbrIdx = grid.toIndex(nbr);

        aoc::map::TerrainType terrain = grid.terrain(nbrIdx);
        if (terrain == aoc::map::TerrainType::Coast)    { appeal += 1; }
        if (terrain == aoc::map::TerrainType::Mountain) { appeal += 1; }

        if (grid.naturalWonder(nbrIdx) != aoc::map::NaturalWonderType::None) { appeal += 2; }

        aoc::map::ImprovementType imp = grid.improvement(nbrIdx);
        if (imp == aoc::map::ImprovementType::Mine) { appeal -= 1; }

        // Adjacent districts
        for (const std::unique_ptr<aoc::game::Player>& p : gameState.players()) {
            for (const std::unique_ptr<aoc::game::City>& city : p->cities()) {
                for (const CityDistrictsComponent::PlacedDistrict& pd : city->districts().districts) {
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
            // Boost current production queue
            ProductionQueueComponent& queue = city.production();
            if (!queue.isEmpty()) {
                queue.addProgress(50.0f);
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
