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
#include "aoc/simulation/tech/CivicTree.hpp"
#include <algorithm>

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

// ============================================================================
// Titles (2026-09-05): recruit and promote named governors
// ============================================================================

int32_t governorTitlesEarned(const aoc::game::Player& player) {
    int32_t completed = 0;
    for (const bool done : player.civics().completedCivics) {
        if (done) { ++completed; }
    }
    return completed / CIVICS_PER_GOVERNOR_TITLE;
}

int32_t governorTitlesSpent(const aoc::game::Player& player) {
    bool seated[static_cast<std::size_t>(GovernorType::Count)] = {};
    int32_t spent = 0;
    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        const CityGovernorComponent& gov = city->governor();
        if (gov.hasNamedGovernor()) {
            seated[static_cast<std::size_t>(gov.assignedGovernor)] = true;
        }
        spent += gov.promotionCount;
    }
    for (std::size_t t = 1; t < static_cast<std::size_t>(GovernorType::Count); ++t) {
        if (seated[t]) { ++spent; }
    }
    return spent;
}

int32_t governorTitlesAvailable(const aoc::game::Player& player) {
    return std::max(0, governorTitlesEarned(player) - governorTitlesSpent(player));
}

int32_t governorFavorPerTurn(const aoc::game::Player& player) {
    int32_t favor = 0;
    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        const CityGovernorComponent& gov = city->governor();
        if (gov.hasPromotion(GovernorPromotion::PeaceKeeper))  { favor += 10; }
        if (gov.hasPromotion(GovernorPromotion::CarbonCredit)) { favor += 5; }
    }
    return favor;
}

ErrorCode requestAssignGovernor(aoc::game::GameState& gameState, PlayerId player,
                                hex::AxialCoord cityAt, GovernorType type) {
    aoc::game::Player* owner = gameState.player(player);
    if (owner == nullptr || type == GovernorType::None || type >= GovernorType::Count) {
        return ErrorCode::InvalidArgument;
    }
    aoc::game::City* city = owner->cityAt(cityAt);
    if (city == nullptr) {
        return ErrorCode::InvalidArgument;
    }
    if (city->governor().assignedGovernor == type) {
        return ErrorCode::Ok;   // already seated here
    }
    // Moving a seated governor is free; recruiting a new one costs a title.
    aoc::game::City* previousSeat = nullptr;
    for (const std::unique_ptr<aoc::game::City>& other : owner->cities()) {
        if (other->governor().assignedGovernor == type) { previousSeat = other.get(); }
    }
    if (previousSeat == nullptr && governorTitlesAvailable(*owner) <= 0) {
        return ErrorCode::InvalidState;
    }
    CityGovernorComponent& gov = city->governor();
    if (previousSeat != nullptr) {
        CityGovernorComponent& from = previousSeat->governor();
        gov.assignedGovernor = from.assignedGovernor;
        gov.promotionCount   = from.promotionCount;
        for (int32_t i = 0; i < 3; ++i) { gov.promotions[i] = from.promotions[i]; }
        from.assignedGovernor = GovernorType::None;
        from.promotionCount   = 0;
        for (int32_t i = 0; i < 3; ++i) { from.promotions[i] = GovernorPromotion::None; }
        from.turnsActive = 0;
    } else {
        gov.assignedGovernor = type;
        gov.promotionCount   = 0;
        for (int32_t i = 0; i < 3; ++i) { gov.promotions[i] = GovernorPromotion::None; }
    }
    gov.turnsActive = 0;
    LOG_INFO("Player %u seats governor %.*s in %s", static_cast<unsigned>(player),
             static_cast<int>(governorTypeName(type).size()), governorTypeName(type).data(),
             city->name().c_str());
    return ErrorCode::Ok;
}

ErrorCode requestPromoteGovernor(aoc::game::GameState& gameState, PlayerId player,
                                 hex::AxialCoord cityAt, GovernorPromotion promotion) {
    aoc::game::Player* owner = gameState.player(player);
    if (owner == nullptr) {
        return ErrorCode::InvalidArgument;
    }
    aoc::game::City* city = owner->cityAt(cityAt);
    if (city == nullptr || !city->governor().hasNamedGovernor()
        || governorForPromotion(promotion) != city->governor().assignedGovernor) {
        return ErrorCode::InvalidArgument;
    }
    CityGovernorComponent& gov = city->governor();
    if (gov.hasPromotion(promotion) || gov.promotionCount >= 3) {
        return ErrorCode::InvalidUnitAction;
    }
    if (governorTitlesAvailable(*owner) <= 0) {
        return ErrorCode::InvalidState;
    }
    static_cast<void>(gov.addPromotion(promotion));
    LOG_INFO("Player %u promotes the %.*s in %s (title %u)", static_cast<unsigned>(player),
             static_cast<int>(governorTypeName(gov.assignedGovernor).size()),
             governorTypeName(gov.assignedGovernor).data(), city->name().c_str(),
             static_cast<unsigned>(promotion));
    return ErrorCode::Ok;
}

void aiSpendGovernorTitles(aoc::game::GameState& gameState, PlayerId player) {
    aoc::game::Player* owner = gameState.player(player);
    if (owner == nullptr || governorTitlesAvailable(*owner) <= 0) {
        return;
    }
    // Seats: capital first, then the most populous cities, in a stable order.
    std::vector<aoc::game::City*> seats;
    for (const std::unique_ptr<aoc::game::City>& city : owner->cities()) {
        seats.push_back(city.get());
    }
    std::stable_sort(seats.begin(), seats.end(),
                     [](const aoc::game::City* a, const aoc::game::City* b) {
                         if (a->isOriginalCapital() != b->isOriginalCapital()) {
                             return a->isOriginalCapital();
                         }
                         return a->population() > b->population();
                     });
    for (aoc::game::City* city : seats) {
        if (governorTitlesAvailable(*owner) <= 0) { return; }
        if (city->governor().hasNamedGovernor()) { continue; }
        GovernorType wanted = GovernorType::Diplomat;
        if (city->isOriginalCapital())                        { wanted = GovernorType::Financier; }
        else if (city->hasDistrict(DistrictType::Campus))     { wanted = GovernorType::Scholar; }
        else if (city->hasDistrict(DistrictType::Industrial)) { wanted = GovernorType::Industrialist; }
        // Each type sits in one city; fall through the list until an unseated one fits.
        const GovernorType order[] = {wanted, GovernorType::Financier, GovernorType::Scholar,
                                      GovernorType::Industrialist, GovernorType::Diplomat,
                                      GovernorType::General, GovernorType::Merchant,
                                      GovernorType::Environmentalist};
        for (const GovernorType candidate : order) {
            bool seatedElsewhere = false;
            for (aoc::game::City* other : seats) {
                if (other->governor().assignedGovernor == candidate) { seatedElsewhere = true; }
            }
            if (seatedElsewhere) { continue; }
            if (requestAssignGovernor(gameState, player, city->location(), candidate) == ErrorCode::Ok) {
                break;
            }
        }
    }
    // Then the first effective title of each seated tree.
    for (aoc::game::City* city : seats) {
        if (governorTitlesAvailable(*owner) <= 0) { return; }
        const CityGovernorComponent& gov = city->governor();
        if (!gov.hasNamedGovernor() || gov.promotionCount >= 3) { continue; }
        for (uint8_t v = 1; v < static_cast<uint8_t>(GovernorPromotion::Count); ++v) {
            const GovernorPromotion promo = static_cast<GovernorPromotion>(v);
            if (governorForPromotion(promo) != gov.assignedGovernor
                || !governorPromotionHasEffect(promo) || gov.hasPromotion(promo)) {
                continue;
            }
            static_cast<void>(requestPromoteGovernor(gameState, player, city->location(), promo));
            break;
        }
    }
}

} // namespace aoc::sim
