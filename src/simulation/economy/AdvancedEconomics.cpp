/**
 * @file AdvancedEconomics.cpp
 * @brief Implementation of advanced economic systems (Batch C: Economics Realism).
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/economy/AdvancedEconomics.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

// ============================================================================
// Tariffs
// ============================================================================

float PlayerTariffComponent::effectiveImportTariff(PlayerId from) const {
    const std::unordered_map<PlayerId, float>::const_iterator it = this->perPlayerTariffs.find(from);
    if (it != this->perPlayerTariffs.end()) {
        return it->second;
    }
    return this->importTariffRate;
}

float PlayerTariffComponent::effectiveTollRate(PlayerId trader) const {
    const std::unordered_map<PlayerId, float>::const_iterator it = this->perPlayerTollRates.find(trader);
    float rate = (it != this->perPlayerTollRates.end()) ? it->second : this->defaultTollRate;
    return std::clamp(rate, 0.0f, 0.5f);
}

float PlayerTariffComponent::effectiveCanalTollRate(PlayerId trader) const {
    const std::unordered_map<PlayerId, float>::const_iterator it = this->perPlayerCanalTollRates.find(trader);
    float rate = (it != this->perPlayerCanalTollRates.end()) ? it->second : this->defaultCanalTollRate;
    return std::clamp(rate, 0.0f, 0.5f);
}


// ============================================================================
// Transport Costs
// ============================================================================


// ============================================================================
// Trade Blocs
// ============================================================================


// ============================================================================
// Technology Spillover
// ============================================================================


// ============================================================================
// Labor Market
// ============================================================================

void CityLaborComponent::autoAssign(int32_t totalPopulation) {
    if (totalPopulation <= 0) {
        this->farmers    = 0;
        this->miners     = 0;
        this->merchants  = 0;
        this->scientists = 0;
        return;
    }

    this->farmers    = static_cast<int32_t>(static_cast<float>(totalPopulation) * 0.4f);
    this->miners     = static_cast<int32_t>(static_cast<float>(totalPopulation) * 0.3f);
    this->merchants  = static_cast<int32_t>(static_cast<float>(totalPopulation) * 0.15f);
    this->scientists = static_cast<int32_t>(static_cast<float>(totalPopulation) * 0.15f);

    const int32_t assigned = this->farmers + this->miners + this->merchants + this->scientists;
    this->farmers += (totalPopulation - assigned);
}

static float laborSpecializationMultiplier(int32_t workers, int32_t totalPop) {
    if (totalPop <= 0) {
        return 1.0f;
    }
    const float fraction = static_cast<float>(workers) / static_cast<float>(totalPop);
    return 1.0f / (1.0f + fraction * 0.3f);
}

float CityLaborComponent::foodMultiplier() const {
    const int32_t total = this->farmers + this->miners + this->merchants + this->scientists;
    return laborSpecializationMultiplier(this->farmers, total);
}

float CityLaborComponent::productionMultiplier() const {
    const int32_t total = this->farmers + this->miners + this->merchants + this->scientists;
    return laborSpecializationMultiplier(this->miners, total);
}

float CityLaborComponent::goldMultiplier() const {
    const int32_t total = this->farmers + this->miners + this->merchants + this->scientists;
    return laborSpecializationMultiplier(this->merchants, total);
}

float CityLaborComponent::scienceMultiplier() const {
    const int32_t total = this->farmers + this->miners + this->merchants + this->scientists;
    return laborSpecializationMultiplier(this->scientists, total);
}

// ============================================================================
// Diminishing Returns
// ============================================================================

float diminishingReturns(int32_t currentStockpile, int32_t newProduction) {
    const float stockpileF = static_cast<float>(currentStockpile);
    const float diminish   = std::min(0.5f, stockpileF / 100.0f);
    return static_cast<float>(newProduction) * (1.0f - diminish);
}

// ============================================================================
// Infrastructure
// ============================================================================

float computeInfrastructureBonus(const aoc::game::GameState& gameState,
                                 const aoc::map::HexGrid& grid,
                                 PlayerId cityOwner,
                                 aoc::hex::AxialCoord cityLocation) {
    constexpr float BONUS_PER_INFRA = 0.05f;
    constexpr float MAX_BONUS       = 1.5f;

    const aoc::game::Player* owner = gameState.player(cityOwner);
    if (owner == nullptr) {
        return 1.0f;
    }
    const aoc::game::City* city = owner->cityAt(cityLocation);
    if (city == nullptr) {
        return 1.0f;
    }

    float bonus = 1.0f;

    for (const hex::AxialCoord& tile : city->workedTiles()) {
        if (!grid.isValid(tile)) {
            continue;
        }
        const int32_t idx = grid.toIndex(tile);
        if (grid.hasRoad(idx)) {
            bonus += BONUS_PER_INFRA;
        }
    }

    const CityDistrictsComponent& districts = city->districts();
    if (districts.hasDistrict(DistrictType::Harbor)) {
        bonus += BONUS_PER_INFRA;
    }
    if (districts.hasBuilding(BuildingId{23})) {
        bonus += BONUS_PER_INFRA;
    }
    if (districts.hasBuilding(BuildingId{6})) {
        bonus += BONUS_PER_INFRA;
    }
    if (districts.hasBuilding(BuildingId{20})) {
        bonus += BONUS_PER_INFRA;
    }

    return std::min(bonus, MAX_BONUS);
}


// ============================================================================
// Currency Exchange
// ============================================================================

float computeExchangeRate(const aoc::game::GameState& gameState,
                          PlayerId playerA, PlayerId playerB) {
    const aoc::game::Player* pA = gameState.player(playerA);
    const aoc::game::Player* pB = gameState.player(playerB);
    if (pA == nullptr || pB == nullptr) {
        return 1.0f;
    }

    const MonetarySystemType systemA   = pA->monetary().system;
    const MonetarySystemType systemB   = pB->monetary().system;
    const float              inflationA = pA->monetary().inflationRate;
    const float              inflationB = pB->monetary().inflationRate;

    if (systemA == MonetarySystemType::Barter && systemB == MonetarySystemType::Barter) {
        return 1.0f;
    }

    if (systemA == systemB) {
        return 1.0f;
    }

    const bool aIsGold = (systemA == MonetarySystemType::GoldStandard ||
                          systemA == MonetarySystemType::CommodityMoney);
    const bool bIsFiat = (systemB == MonetarySystemType::FiatMoney
                          || systemB == MonetarySystemType::Digital);
    const bool bIsGold = (systemB == MonetarySystemType::GoldStandard ||
                          systemB == MonetarySystemType::CommodityMoney);
    const bool aIsFiat = (systemA == MonetarySystemType::FiatMoney
                          || systemA == MonetarySystemType::Digital);

    if (aIsGold && bIsFiat) {
        return 1.0f + inflationB * 2.0f;
    }
    if (bIsGold && aIsFiat) {
        return 1.0f / (1.0f + inflationA * 2.0f);
    }

    return 0.8f;
}


// ============================================================================
// Master function
// ============================================================================


} // namespace aoc::sim
