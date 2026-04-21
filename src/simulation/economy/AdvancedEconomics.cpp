/**
 * @file AdvancedEconomics.cpp
 * @brief Implementation of advanced economic systems (Batch C: Economics Realism).
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/economy/AdvancedEconomics.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/economy/TradeRoute.hpp"
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

float applyTariffs(const PlayerTariffComponent& importer,
                   PlayerId exporter, float baseValue) {
    const float tariffRate  = importer.effectiveImportTariff(exporter);
    const float clampedRate = std::clamp(tariffRate, 0.0f, 0.5f);
    return baseValue * (1.0f - clampedRate);
}

// ============================================================================
// Transport Costs
// ============================================================================

float computeTransportCost(const aoc::map::HexGrid& grid,
                           hex::AxialCoord from, hex::AxialCoord to,
                           float baseGoodValue) {
    const int32_t dist = grid.distance(from, to);
    float cost = static_cast<float>(dist) * 0.02f * baseGoodValue;

    if (grid.isValid(from) && grid.isValid(to)) {
        const int32_t fromIdx = grid.toIndex(from);
        const int32_t toIdx   = grid.toIndex(to);

        const bool fromCoastal = (grid.terrain(fromIdx) == aoc::map::TerrainType::Coast);
        const bool toCoastal   = (grid.terrain(toIdx)   == aoc::map::TerrainType::Coast);
        if (fromCoastal || toCoastal) {
            cost *= 0.7f;
        }

        const bool fromRoad = grid.hasRoad(fromIdx);
        const bool toRoad   = grid.hasRoad(toIdx);
        if (fromRoad && toRoad) {
            cost *= 0.5f;
        }
    }

    return std::max(0.0f, cost);
}

// ============================================================================
// Trade Blocs
// ============================================================================

bool GlobalTradeBlocTracker::areInSameBloc(PlayerId a, PlayerId b) const {
    for (const TradeBloc& bloc : this->blocs) {
        bool hasA = false;
        bool hasB = false;
        for (const PlayerId member : bloc.members) {
            if (member == a) { hasA = true; }
            if (member == b) { hasB = true; }
        }
        if (hasA && hasB) {
            return true;
        }
    }
    return false;
}

float GlobalTradeBlocTracker::effectiveTariff(PlayerId importer, PlayerId exporter) const {
    for (const TradeBloc& bloc : this->blocs) {
        bool hasImporter = false;
        bool hasExporter = false;
        for (const PlayerId member : bloc.members) {
            if (member == importer) { hasImporter = true; }
            if (member == exporter) { hasExporter = true; }
        }
        if (hasImporter && hasExporter) {
            return bloc.internalTariff;
        }
        if (hasImporter && !hasExporter) {
            return bloc.externalTariff;
        }
    }
    return 0.0f;
}

// ============================================================================
// Technology Spillover
// ============================================================================

float computeTechSpillover(const aoc::game::GameState& gameState,
                           PlayerId player, PlayerId tradePartner) {
    constexpr float SPILLOVER_RATE = 0.5f;

    const aoc::game::Player* myPlayer      = gameState.player(player);
    const aoc::game::Player* partnerPlayer = gameState.player(tradePartner);
    if (myPlayer == nullptr || partnerPlayer == nullptr) {
        return 0.0f;
    }

    int32_t myTechs      = 0;
    int32_t partnerTechs = 0;

    for (uint16_t t = 0; t < techCount(); ++t) {
        if (myPlayer->tech().hasResearched(TechId{t})) {
            ++myTechs;
        }
        if (partnerPlayer->tech().hasResearched(TechId{t})) {
            ++partnerTechs;
        }
    }

    const int32_t techDiff = partnerTechs - myTechs;
    if (techDiff <= 0) {
        return 0.0f;
    }

    return static_cast<float>(techDiff) * SPILLOVER_RATE;
}

void processTechSpillover(aoc::game::GameState& gameState) {
    const std::vector<TradeRouteComponent>& tradeRoutes = gameState.tradeRoutes();

    std::unordered_map<PlayerId, float> spilloverAccum;

    for (const TradeRouteComponent& route : tradeRoutes) {
        const float spilloverA = computeTechSpillover(gameState, route.sourcePlayer, route.destPlayer);
        const float spilloverB = computeTechSpillover(gameState, route.destPlayer, route.sourcePlayer);

        if (spilloverA > 0.0f) {
            spilloverAccum[route.sourcePlayer] += spilloverA;
        }
        if (spilloverB > 0.0f) {
            spilloverAccum[route.destPlayer] += spilloverB;
        }
    }

    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }
        const std::unordered_map<PlayerId, float>::const_iterator it =
            spilloverAccum.find(playerPtr->id());
        if (it != spilloverAccum.end() && it->second > 0.0f) {
            playerPtr->tech().researchProgress += it->second;
            LOG_DEBUG("Tech spillover: player %u gains %.1f science from trade",
                      static_cast<unsigned>(playerPtr->id()),
                      static_cast<double>(it->second));
        }
    }
}

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
// Credit / Banking
// ============================================================================

void PlayerBankingComponent::takeLoan(CurrencyAmount amount) {
    if (amount <= 0) {
        return;
    }
    this->totalLoans       += amount;
    this->turnsUntilPayment = 5;
    LOG_INFO("Player %u took loan of %lld (total debt: %lld)",
             static_cast<unsigned>(this->owner),
             static_cast<long long>(amount),
             static_cast<long long>(this->totalLoans));
}

void PlayerBankingComponent::processPayments(CurrencyAmount& treasury, CurrencyAmount gdp) {
    if (this->totalLoans <= 0) {
        this->hasBankingCrisis      = false;
        this->crisisTurnsRemaining  = 0;
        return;
    }

    const CurrencyAmount interest = this->totalLoans * this->loanInterestRate / 100;
    treasury -= interest;

    LOG_DEBUG("Player %u pays %lld interest on %lld debt",
              static_cast<unsigned>(this->owner),
              static_cast<long long>(interest),
              static_cast<long long>(this->totalLoans));

    if (gdp > 0 && this->totalLoans > 2 * gdp) {
        if (!this->hasBankingCrisis) {
            this->hasBankingCrisis     = true;
            this->crisisTurnsRemaining = 10;
            LOG_ERROR("Player %u enters banking crisis! Debt %lld > 2 * GDP %lld",
                      static_cast<unsigned>(this->owner),
                      static_cast<long long>(this->totalLoans),
                      static_cast<long long>(gdp));
        }
    }

    if (this->hasBankingCrisis) {
        --this->crisisTurnsRemaining;
        if (this->crisisTurnsRemaining <= 0) {
            this->hasBankingCrisis     = false;
            this->crisisTurnsRemaining = 0;
            LOG_INFO("Player %u banking crisis resolved", static_cast<unsigned>(this->owner));
        }
    }
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
// Debt Crisis
// ============================================================================

bool checkDebtCrisis(aoc::game::GameState& gameState, PlayerId player) {
    aoc::game::Player* playerObj = gameState.player(player);
    if (playerObj == nullptr) {
        return false;
    }

    MonetaryStateComponent& state = playerObj->monetary();
    if (state.gdp <= 0) {
        return false;
    }

    const bool inCrisis = (state.governmentDebt > 2 * state.gdp);
    if (!inCrisis) {
        return false;
    }

    LOG_INFO("Debt crisis for player %u: debt %lld > 2 * GDP %lld",
             static_cast<unsigned>(player),
             static_cast<long long>(state.governmentDebt),
             static_cast<long long>(state.gdp));

    PlayerBankingComponent& bank = playerObj->banking();
    if (!bank.hasBankingCrisis) {
        bank.hasBankingCrisis     = true;
        bank.crisisTurnsRemaining = 10;
    }

    return true;
}

// ============================================================================
// Master function
// ============================================================================

void processAdvancedEconomics(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid,
                              PlayerId player, Market& /*market*/) {
    processTechSpillover(gameState);

    aoc::game::Player* playerObj = gameState.player(player);
    if (playerObj != nullptr) {
        PlayerBankingComponent& bank = playerObj->banking();
        CurrencyAmount gdp           = playerObj->monetary().gdp;
        bank.processPayments(playerObj->economy().treasury, gdp);
    }

    [[maybe_unused]] const bool inDebtCrisis = checkDebtCrisis(gameState, player);

    // Infrastructure bonus is applied per-city during production processing; skip here.
    (void)grid;
}

} // namespace aoc::sim
