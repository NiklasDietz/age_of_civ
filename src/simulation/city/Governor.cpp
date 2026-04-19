/**
 * @file Governor.cpp
 * @brief City governor system: automated city management based on focus.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/city/Governor.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/tech/TechGating.hpp"
#include "aoc/simulation/turn/GameLength.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/core/Log.hpp"

namespace aoc::sim {

namespace {

/// Score a building for a given focus.
float scoreBuildingForFocus(CityFocus focus, const BuildingDef& bdef) {
    float score = 10.0f;  // Base

    switch (focus) {
        case CityFocus::Growth:
            // Granary(15), Hospital(22) are growth buildings
            if (bdef.id.value == 15 || bdef.id.value == 22) { score += 60.0f; }
            break;
        case CityFocus::Production:
            score += static_cast<float>(bdef.productionBonus) * 15.0f;
            break;
        case CityFocus::Science:
            score += static_cast<float>(bdef.scienceBonus) * 15.0f;
            score += (bdef.scienceMultiplier - 1.0f) * 100.0f;
            break;
        case CityFocus::Gold:
            score += static_cast<float>(bdef.goldBonus) * 15.0f;
            break;
        case CityFocus::Military:
            // Walls(17), Barracks(18) get high military scores
            if (bdef.id.value == 17 || bdef.id.value == 18) {
                score += 50.0f;
            }
            break;
        case CityFocus::Balanced:
            score += static_cast<float>(bdef.productionBonus) * 8.0f;
            score += static_cast<float>(bdef.scienceBonus) * 8.0f;
            score += static_cast<float>(bdef.goldBonus) * 8.0f;
            break;
        default:
            break;
    }

    // Penalize high maintenance
    score -= static_cast<float>(bdef.maintenanceCost) * 5.0f;

    return score;
}

/// Score a district type for a given focus.
float scoreDistrictForFocus(CityFocus focus, DistrictType dtype) {
    switch (focus) {
        case CityFocus::Science:
            if (dtype == DistrictType::Campus) { return 100.0f; }
            if (dtype == DistrictType::Commercial) { return 40.0f; }
            break;
        case CityFocus::Gold:
            if (dtype == DistrictType::Commercial) { return 100.0f; }
            if (dtype == DistrictType::Harbor) { return 60.0f; }
            break;
        case CityFocus::Production:
            if (dtype == DistrictType::Industrial) { return 100.0f; }
            if (dtype == DistrictType::Commercial) { return 30.0f; }
            break;
        case CityFocus::Military:
            if (dtype == DistrictType::Encampment) { return 100.0f; }
            if (dtype == DistrictType::Industrial) { return 50.0f; }
            break;
        case CityFocus::Balanced:
            if (dtype == DistrictType::Commercial) { return 60.0f; }
            if (dtype == DistrictType::Campus) { return 55.0f; }
            if (dtype == DistrictType::Industrial) { return 50.0f; }
            if (dtype == DistrictType::Encampment) { return 30.0f; }
            if (dtype == DistrictType::Harbor) { return 25.0f; }
            break;
        default:
            break;
    }
    return 20.0f;
}

} // anonymous namespace

void governorAutoQueue(aoc::game::GameState& gameState,
                        const aoc::map::HexGrid& grid,
                        aoc::game::City& city,
                        PlayerId player) {
    aoc::sim::CityGovernorComponent& governor = city.governor();
    if (!governor.isActive || !governor.autoQueueProduction) {
        return;
    }

    aoc::sim::ProductionQueueComponent& queue = city.production();
    if (!queue.isEmpty()) {
        return;
    }

    const CityFocus focus = governor.focus;

    // Check if template is active and has entries
    if (queue.templateActive && !queue.productionTemplate.empty()) {
        const ProductionTemplateEntry& tmpl = queue.productionTemplate.front();
        ProductionQueueItem item;
        item.type = tmpl.type;
        item.itemId = tmpl.itemId;
        item.name = tmpl.name;
        item.totalCost = tmpl.baseCost * GamePace::instance().costMultiplier;
        item.progress = 0.0f;
        queue.queue.push_back(std::move(item));

        // Rotate template (move first entry to end)
        ProductionTemplateEntry front = std::move(queue.productionTemplate.front());
        queue.productionTemplate.erase(queue.productionTemplate.begin());
        queue.productionTemplate.push_back(std::move(front));
        return;
    }

    const aoc::sim::CityDistrictsComponent& districts = city.districts();

    // Step 1: Check if we need a district
    bool needsDistrict = false;
    DistrictType bestDistrict = DistrictType::CityCenter;
    float bestDistrictScore = -1.0f;

    const int32_t districtCount = static_cast<int32_t>(districts.districts.size());
    int32_t buildingCount = 0;
    for (const CityDistrictsComponent::PlacedDistrict& d : districts.districts) {
        buildingCount += static_cast<int32_t>(d.buildings.size());
    }

    // Build a district if we have enough buildings in existing districts
    if (districtCount <= 1 || buildingCount >= districtCount - 1) {
        constexpr DistrictType ALL_DISTRICTS[] = {
            DistrictType::Commercial, DistrictType::Campus,
            DistrictType::Industrial, DistrictType::Encampment, DistrictType::Harbor
        };
        for (DistrictType dtype : ALL_DISTRICTS) {
            if (districts.hasDistrict(dtype)) { continue; }
            float score = scoreDistrictForFocus(focus, dtype);
            if (score > bestDistrictScore) {
                bestDistrictScore = score;
                bestDistrict = dtype;
                needsDistrict = true;
            }
        }
    }

    // Step 2: Score best building using the City& overload of canBuildBuilding.
    float bestBuildingScore = -1.0f;
    BuildingId bestBuildingId{0};
    for (uint16_t bidx = 0; bidx < static_cast<uint16_t>(BUILDING_DEFS.size()); ++bidx) {
        const BuildingDef& bdef = BUILDING_DEFS[bidx];
        if (!canBuildBuilding(gameState, player, city, bdef.id, &grid)) { continue; }
        float score = scoreBuildingForFocus(focus, bdef);
        if (score > bestBuildingScore) {
            bestBuildingScore = score;
            bestBuildingId = bdef.id;
        }
    }

    // Step 3: Decide district vs building vs unit
    ProductionQueueItem item;

    if (needsDistrict && bestDistrictScore > bestBuildingScore * 0.8f) {
        item.type = ProductionItemType::District;
        item.itemId = static_cast<uint16_t>(bestDistrict);
        item.name = std::string(districtTypeName(bestDistrict));
        item.totalCost = 60.0f;
    } else if (bestBuildingScore > 0.0f) {
        const BuildingDef& bdef = BUILDING_DEFS[bestBuildingId.value];
        item.type = ProductionItemType::Building;
        item.itemId = bdef.id.value;
        item.name = std::string(bdef.name);
        item.totalCost = static_cast<float>(bdef.productionCost);
    } else if (focus == CityFocus::Military) {
        item.type = ProductionItemType::Unit;
        item.itemId = 0;  // Warrior fallback
        item.name = "Warrior";
        item.totalCost = 40.0f;
    } else {
        // Nothing to build: produce a Builder
        item.type = ProductionItemType::Unit;
        item.itemId = 5;
        item.name = "Builder";
        item.totalCost = 50.0f;
    }

    item.totalCost *= GamePace::instance().costMultiplier;
    item.progress = 0.0f;
    queue.queue.push_back(std::move(item));

    (void)grid;
}

void processGovernors(aoc::game::GameState& gameState,
                       const aoc::map::HexGrid& grid,
                       PlayerId player) {
    aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) {
        return;
    }

    for (const std::unique_ptr<aoc::game::City>& city : gsPlayer->cities()) {
        governorAutoQueue(gameState, grid, *city, player);
    }
}

} // namespace aoc::sim
