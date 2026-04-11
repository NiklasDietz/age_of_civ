/**
 * @file AdvancedEconomics.cpp
 * @brief Implementation of advanced economic systems (Batch C: Economics Realism).
 */

#include "aoc/game/GameState.hpp"
#include "aoc/simulation/economy/AdvancedEconomics.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/economy/TradeRoute.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/ecs/World.hpp"
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

float applyTariffs(const PlayerTariffComponent& importer,
                   PlayerId exporter, float baseValue) {
    const float tariffRate = importer.effectiveImportTariff(exporter);
    const float clampedRate = std::clamp(tariffRate, 0.0f, 0.5f);
    return baseValue * (1.0f - clampedRate);
}

// ============================================================================
// Transport Costs
// ============================================================================

float computeTransportCost(const aoc::map::HexGrid& grid,
                           hex::AxialCoord from, hex::AxialCoord to,
                           float baseGoodValue) {
    const int32_t dist = hex::distance(from, to);
    float cost = static_cast<float>(dist) * 0.02f * baseGoodValue;

    // Check if origin or destination is coastal (harbors reduce cost by 30%)
    if (grid.isValid(from) && grid.isValid(to)) {
        const int32_t fromIdx = grid.toIndex(from);
        const int32_t toIdx = grid.toIndex(to);

        const bool fromCoastal = (grid.terrain(fromIdx) == aoc::map::TerrainType::Coast);
        const bool toCoastal = (grid.terrain(toIdx) == aoc::map::TerrainType::Coast);
        if (fromCoastal || toCoastal) {
            cost *= 0.7f;
        }

        // Check for roads at origin and destination (roads reduce cost by 50%)
        const bool fromRoad = grid.hasRoad(fromIdx);
        const bool toRoad = grid.hasRoad(toIdx);
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
            if (member == a) {
                hasA = true;
            }
            if (member == b) {
                hasB = true;
            }
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
            if (member == importer) {
                hasImporter = true;
            }
            if (member == exporter) {
                hasExporter = true;
            }
        }
        if (hasImporter && hasExporter) {
            return bloc.internalTariff;
        }
        if (hasImporter && !hasExporter) {
            return bloc.externalTariff;
        }
    }
    return 0.0f;  // No bloc involvement, no bloc-level tariff
}

// ============================================================================
// Technology Spillover
// ============================================================================

float computeTechSpillover(const aoc::game::GameState& gameState,
                           PlayerId player, PlayerId tradePartner) {
    aoc::ecs::World& world = gameState.legacyWorld();
    constexpr float SPILLOVER_RATE = 0.5f;

    int32_t myTechs = 0;
    int32_t partnerTechs = 0;

    const aoc::ecs::ComponentPool<PlayerTechComponent>* techPool =
        world.getPool<PlayerTechComponent>();
    if (techPool == nullptr) {
        return 0.0f;
    }

    for (uint32_t i = 0; i < techPool->size(); ++i) {
        const PlayerTechComponent& tech = techPool->data()[i];
        int32_t completedCount = 0;
        for (uint16_t t = 0; t < techCount(); ++t) {
            if (tech.hasResearched(TechId{t})) {
                ++completedCount;
            }
        }
        if (tech.owner == player) {
            myTechs = completedCount;
        }
        if (tech.owner == tradePartner) {
            partnerTechs = completedCount;
        }
    }

    const int32_t techDiff = partnerTechs - myTechs;
    if (techDiff <= 0) {
        return 0.0f;
    }

    return static_cast<float>(techDiff) * SPILLOVER_RATE;
}

void processTechSpillover(aoc::game::GameState& gameState) {
    const aoc::ecs::ComponentPool<TradeRouteComponent>* tradePool =
        world.getPool<TradeRouteComponent>();
    if (tradePool == nullptr) {
        return;
    }

    // Accumulate spillover per player from all active trade routes
    std::unordered_map<PlayerId, float> spilloverAccum;

    for (uint32_t i = 0; i < tradePool->size(); ++i) {
        const TradeRouteComponent& route = tradePool->data()[i];
        const float spilloverA = computeTechSpillover(world, route.sourcePlayer, route.destPlayer);
        const float spilloverB = computeTechSpillover(world, route.destPlayer, route.sourcePlayer);

        if (spilloverA > 0.0f) {
            spilloverAccum[route.sourcePlayer] += spilloverA;
        }
        if (spilloverB > 0.0f) {
            spilloverAccum[route.destPlayer] += spilloverB;
        }
    }

    // Apply accumulated spillover as bonus science to each player's research
    aoc::ecs::ComponentPool<PlayerTechComponent>* techPool =
        world.getPool<PlayerTechComponent>();
    if (techPool == nullptr) {
        return;
    }

    for (uint32_t i = 0; i < techPool->size(); ++i) {
        PlayerTechComponent& tech = techPool->data()[i];
        const std::unordered_map<PlayerId, float>::const_iterator it = spilloverAccum.find(tech.owner);
        if (it != spilloverAccum.end() && it->second > 0.0f) {
            tech.researchProgress += it->second;
            LOG_DEBUG("Tech spillover: player %u gains %.1f science from trade",
                      static_cast<unsigned>(tech.owner), static_cast<double>(it->second));
        }
    }
}

// ============================================================================
// Labor Market
// ============================================================================

void CityLaborComponent::autoAssign(int32_t totalPopulation) {
    if (totalPopulation <= 0) {
        this->farmers = 0;
        this->miners = 0;
        this->merchants = 0;
        this->scientists = 0;
        return;
    }

    // Base distribution: 40% farmers, 30% miners, 15% merchants, 15% scientists
    this->farmers    = static_cast<int32_t>(static_cast<float>(totalPopulation) * 0.4f);
    this->miners     = static_cast<int32_t>(static_cast<float>(totalPopulation) * 0.3f);
    this->merchants  = static_cast<int32_t>(static_cast<float>(totalPopulation) * 0.15f);
    this->scientists = static_cast<int32_t>(static_cast<float>(totalPopulation) * 0.15f);

    // Assign remaining citizens (rounding remainder) to farmers
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
    const float diminish = std::min(0.5f, stockpileF / 100.0f);
    return static_cast<float>(newProduction) * (1.0f - diminish);
}

// ============================================================================
// Infrastructure
// ============================================================================

float computeInfrastructureBonus(const aoc::game::GameState& gameState,
                                 const aoc::map::HexGrid& grid,
                                 EntityId cityEntity) {
    aoc::ecs::World& world = gameState.legacyWorld();
    constexpr float BONUS_PER_INFRA = 0.05f;
    constexpr float MAX_BONUS = 1.5f;

    const CityComponent* city = world.tryGetComponent<CityComponent>(cityEntity);
    if (city == nullptr) {
        return 1.0f;
    }

    float bonus = 1.0f;

    // Count road tiles in worked tiles
    for (const hex::AxialCoord& tile : city->workedTiles) {
        if (!grid.isValid(tile)) {
            continue;
        }
        const int32_t idx = grid.toIndex(tile);
        if (grid.hasRoad(idx)) {
            bonus += BONUS_PER_INFRA;
        }
    }

    // Check for harbor and market buildings
    const CityDistrictsComponent* districts =
        world.tryGetComponent<CityDistrictsComponent>(cityEntity);
    if (districts != nullptr) {
        // Harbor district (DistrictType::Harbor)
        if (districts->hasDistrict(DistrictType::Harbor)) {
            bonus += BONUS_PER_INFRA;
        }
        // Shipyard building (BuildingId{23})
        if (districts->hasBuilding(BuildingId{23})) {
            bonus += BONUS_PER_INFRA;
        }
        // Market building (BuildingId{6})
        if (districts->hasBuilding(BuildingId{6})) {
            bonus += BONUS_PER_INFRA;
        }
        // Bank building (BuildingId{20})
        if (districts->hasBuilding(BuildingId{20})) {
            bonus += BONUS_PER_INFRA;
        }
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
    this->totalLoans += amount;
    this->turnsUntilPayment = 5;  // First interest payment in 5 turns
    LOG_INFO("Player %u took loan of %lld (total debt: %lld)",
             static_cast<unsigned>(this->owner),
             static_cast<long long>(amount),
             static_cast<long long>(this->totalLoans));
}

void PlayerBankingComponent::processPayments(CurrencyAmount& treasury, CurrencyAmount gdp) {
    if (this->totalLoans <= 0) {
        this->hasBankingCrisis = false;
        this->crisisTurnsRemaining = 0;
        return;
    }

    // Process interest each turn
    const CurrencyAmount interest =
        this->totalLoans * this->loanInterestRate / 100;
    treasury -= interest;

    LOG_DEBUG("Player %u pays %lld interest on %lld debt",
              static_cast<unsigned>(this->owner),
              static_cast<long long>(interest),
              static_cast<long long>(this->totalLoans));

    // Debt crisis check: debt > 2 * GDP
    if (gdp > 0 && this->totalLoans > 2 * gdp) {
        if (!this->hasBankingCrisis) {
            this->hasBankingCrisis = true;
            this->crisisTurnsRemaining = 10;
            LOG_ERROR("Player %u enters banking crisis! Debt %lld > 2 * GDP %lld",
                      static_cast<unsigned>(this->owner),
                      static_cast<long long>(this->totalLoans),
                      static_cast<long long>(gdp));
        }
    }

    // Tick down crisis
    if (this->hasBankingCrisis) {
        --this->crisisTurnsRemaining;
        if (this->crisisTurnsRemaining <= 0) {
            this->hasBankingCrisis = false;
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
    aoc::ecs::World& world = gameState.legacyWorld();
    const aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();
    if (monetaryPool == nullptr) {
        return 1.0f;
    }

    MonetarySystemType systemA = MonetarySystemType::Barter;
    MonetarySystemType systemB = MonetarySystemType::Barter;
    float inflationA = 0.0f;
    float inflationB = 0.0f;

    for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
        const MonetaryStateComponent& state = monetaryPool->data()[i];
        if (state.owner == playerA) {
            systemA = state.system;
            inflationA = state.inflationRate;
        }
        if (state.owner == playerB) {
            systemB = state.system;
            inflationB = state.inflationRate;
        }
    }

    // Barter-Barter: direct trade at 1:1
    if (systemA == MonetarySystemType::Barter && systemB == MonetarySystemType::Barter) {
        return 1.0f;
    }

    // Matching systems: trade at 1:1
    if (systemA == systemB) {
        return 1.0f;
    }

    // Gold Standard vs Fiat: influenced by inflation
    const bool aIsGold = (systemA == MonetarySystemType::GoldStandard ||
                          systemA == MonetarySystemType::CommodityMoney);
    const bool bIsFiat = (systemB == MonetarySystemType::FiatMoney);
    const bool bIsGold = (systemB == MonetarySystemType::GoldStandard ||
                          systemB == MonetarySystemType::CommodityMoney);
    const bool aIsFiat = (systemA == MonetarySystemType::FiatMoney);

    if (aIsGold && bIsFiat) {
        return 1.0f + inflationB * 2.0f;
    }
    if (bIsGold && aIsFiat) {
        return 1.0f / (1.0f + inflationA * 2.0f);
    }

    // Different non-matching systems: 0.8 base rate
    return 0.8f;
}

// ============================================================================
// Debt Crisis
// ============================================================================

bool checkDebtCrisis(aoc::game::GameState& gameState, PlayerId player) {
    aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();
    if (monetaryPool == nullptr) {
        return false;
    }

    for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
        MonetaryStateComponent& state = monetaryPool->data()[i];
        if (state.owner != player) {
            continue;
        }

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

        // Apply penalties via the banking component if present
        aoc::ecs::ComponentPool<PlayerBankingComponent>* bankPool =
            world.getPool<PlayerBankingComponent>();
        if (bankPool != nullptr) {
            for (uint32_t j = 0; j < bankPool->size(); ++j) {
                PlayerBankingComponent& bank = bankPool->data()[j];
                if (bank.owner == player && !bank.hasBankingCrisis) {
                    bank.hasBankingCrisis = true;
                    bank.crisisTurnsRemaining = 10;
                }
            }
        }

        return true;
    }

    return false;
}

// ============================================================================
// Master function
// ============================================================================

void processAdvancedEconomics(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid,
                              PlayerId player, Market& /*market*/) {
    aoc::ecs::World& world = gameState.legacyWorld();
    // 1. Tech spillover from trade routes
    processTechSpillover(world);

    // 2. Banking payments
    aoc::ecs::ComponentPool<PlayerBankingComponent>* bankPool =
        world.getPool<PlayerBankingComponent>();
    if (bankPool != nullptr) {
        for (uint32_t i = 0; i < bankPool->size(); ++i) {
            PlayerBankingComponent& bank = bankPool->data()[i];
            if (bank.owner != player) {
                continue;
            }

            // Find player treasury and GDP
            CurrencyAmount gdp = 0;
            aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
                world.getPool<MonetaryStateComponent>();
            if (monetaryPool != nullptr) {
                for (uint32_t j = 0; j < monetaryPool->size(); ++j) {
                    if (monetaryPool->data()[j].owner == player) {
                        gdp = monetaryPool->data()[j].gdp;
                        break;
                    }
                }
            }

            aoc::ecs::ComponentPool<PlayerEconomyComponent>* econPool =
                world.getPool<PlayerEconomyComponent>();
            if (econPool != nullptr) {
                for (uint32_t j = 0; j < econPool->size(); ++j) {
                    if (econPool->data()[j].owner == player) {
                        bank.processPayments(econPool->data()[j].treasury, gdp);
                        break;
                    }
                }
            }
        }
    }

    // 3. Debt crisis check
    [[maybe_unused]] const bool inDebtCrisis = checkDebtCrisis(world, player);

    // 4. Infrastructure bonus application to cities
    aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    if (cityPool != nullptr) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            const CityComponent& city = cityPool->data()[i];
            if (city.owner != player) {
                continue;
            }
            const EntityId cityEntity = cityPool->entities()[i];
            const float infraBonus = computeInfrastructureBonus(world, grid, cityEntity);
            if (infraBonus > 1.0f) {
                LOG_DEBUG("City %s infrastructure bonus: %.2f",
                          city.name.c_str(), static_cast<double>(infraBonus));
            }
        }
    }
}

} // namespace aoc::sim
