/**
 * @file AIEconomicStrategy.cpp
 * @brief AI economic strategy: bonds, sanctions, devaluation, infrastructure, crises.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/simulation/ai/AIEconomicStrategy.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/monetary/CentralBank.hpp"
#include "aoc/simulation/monetary/CurrencyCrisis.hpp"
#include "aoc/simulation/monetary/CurrencyWar.hpp"
#include "aoc/simulation/monetary/Bonds.hpp"
#include "aoc/simulation/monetary/CurrencyTrust.hpp"
#include "aoc/simulation/economy/Sanctions.hpp"
#include "aoc/simulation/economy/Speculation.hpp"
#include "aoc/simulation/economy/EconomicDepth.hpp"
#include "aoc/simulation/economy/IndustrialRevolution.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/economy/TradeRoute.hpp"
#include "aoc/simulation/production/PowerGrid.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim {

// ============================================================================
// Helper: find player's monetary state
// ============================================================================

static MonetaryStateComponent* findMonetary(aoc::game::GameState& gameState, PlayerId player) {
    aoc::ecs::ComponentPool<MonetaryStateComponent>* pool =
        world.getPool<MonetaryStateComponent>();
    if (pool == nullptr) { return nullptr; }
    for (uint32_t i = 0; i < pool->size(); ++i) {
        if (pool->data()[i].owner == player) { return &pool->data()[i]; }
    }
    return nullptr;
}

// ============================================================================
// Bond strategy
// ============================================================================

static void aiBondStrategy(aoc::game::GameState& gameState, PlayerId player,
                           int32_t difficulty) {
    aoc::ecs::World& world = gameState.legacyWorld();
    MonetaryStateComponent* myState = findMonetary(world, player);
    if (myState == nullptr) { return; }
    aoc::ecs::World& world = gameState.legacyWorld();

    // Buy bonds from weaker players (investment + leverage)
    // Only on normal/hard difficulty
    if (difficulty < 1) { return; }

    aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();
    if (monetaryPool == nullptr) { return; }

    for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
        const MonetaryStateComponent& other = monetaryPool->data()[i];
        if (other.owner == player) { continue; }

        // Buy bonds if we have surplus treasury and the other player has lower GDP
        if (myState->treasury > 200 && other.gdp < myState->gdp) {
            CurrencyAmount investAmount = std::min(
                myState->treasury / 4,
                static_cast<CurrencyAmount>(100));
            if (investAmount > 20) {
                issueBond(world, other.owner, player, investAmount);
            }
        }
    }
}

// ============================================================================
// Sanctions strategy
// ============================================================================

static void aiSanctionStrategy(aoc::game::GameState& gameState,
                                DiplomacyManager& diplomacy,
                                PlayerId player,
                                int32_t difficulty) {
    aoc::ecs::World& world = gameState.legacyWorld();
    if (difficulty < 2) { return; }  // Only hard AI uses sanctions
    aoc::ecs::World& world = gameState.legacyWorld();

    // Sanction players we're at war with (if we have financial leverage)
    const aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();
    if (monetaryPool == nullptr) { return; }

    for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
        PlayerId other = monetaryPool->data()[i].owner;
        if (other == player) { continue; }

        if (diplomacy.isAtWar(player, other)) {
            // Impose financial sanctions if we haven't already
            // (would need access to the global sanction tracker from EconomySimulation)
            LOG_INFO("AI player %u considering sanctions against player %u",
                     static_cast<unsigned>(player), static_cast<unsigned>(other));
        }
    }
}

// ============================================================================
// Insurance strategy
// ============================================================================

static void aiInsuranceStrategy(aoc::game::GameState& gameState, PlayerId player) {
    // AI buys insurance if it can afford the premiums and has vulnerable assets
    MonetaryStateComponent* myState = findMonetary(world, player);
    if (myState == nullptr || myState->treasury < 100) { return; }
    aoc::ecs::World& world = gameState.legacyWorld();

    aoc::ecs::ComponentPool<PlayerInsuranceComponent>* insPool =
        world.getPool<PlayerInsuranceComponent>();
    if (insPool == nullptr) { return; }

    for (uint32_t i = 0; i < insPool->size(); ++i) {
        if (insPool->data()[i].owner == player) {
            PlayerInsuranceComponent& ins = insPool->data()[i];
            // Buy all insurance if treasury is healthy
            if (myState->treasury > 300) {
                ins.hasWarInsurance = true;
                ins.hasTradeInsurance = true;
                ins.hasDisasterInsurance = true;
            } else if (myState->treasury > 150) {
                ins.hasTradeInsurance = true;
            }
            break;
        }
    }
}

// ============================================================================
// Immigration policy
// ============================================================================

static void aiImmigrationPolicy(aoc::game::GameState& gameState, PlayerId player) {
    aoc::ecs::ComponentPool<PlayerMigrationComponent>* migPool =
        world.getPool<PlayerMigrationComponent>();
    if (migPool == nullptr) { return; }
    aoc::ecs::World& world = gameState.legacyWorld();

    // Simple strategy: open borders if small, controlled if medium, closed if at war
    int32_t cityCount = 0;
    const aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    if (cityPool != nullptr) {
        for (uint32_t c = 0; c < cityPool->size(); ++c) {
            if (cityPool->data()[c].owner == player) { ++cityCount; }
        }
    }

    for (uint32_t i = 0; i < migPool->size(); ++i) {
        if (migPool->data()[i].owner == player) {
            if (cityCount < 4) {
                migPool->data()[i].policy = ImmigrationPolicy::Open;
            } else if (cityCount < 8) {
                migPool->data()[i].policy = ImmigrationPolicy::Controlled;
            } else {
                migPool->data()[i].policy = ImmigrationPolicy::Closed;
            }
            break;
        }
    }
}

// ============================================================================
// Power grid management
// ============================================================================

void aiManagePowerGrid(aoc::game::GameState& gameState,
                       const aoc::map::HexGrid& grid,
                       PlayerId player) {
    aoc::ecs::World& world = gameState.legacyWorld();
    const aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    if (cityPool == nullptr) { return; }

    for (uint32_t c = 0; c < cityPool->size(); ++c) {
        if (cityPool->data()[c].owner != player) { continue; }
        EntityId cityEntity = cityPool->entities()[c];

        CityPowerComponent power = computeCityPower(
            const_cast<aoc::ecs::World&>(world), grid, cityEntity);

        if (power.energyDemand <= power.energySupply) {
            continue;  // Power is sufficient
        }

        // Need more power. Check if city already has a power plant queued.
        // For now, just log the need (actual queuing requires production system access)
        LOG_INFO("AI: city needs %d more energy (demand %d, supply %d)",
                 power.energyDemand - power.energySupply,
                 power.energyDemand, power.energySupply);
    }
}

// ============================================================================
// Infrastructure management
// ============================================================================

void aiManageInfrastructure(aoc::game::GameState& gameState,
                            aoc::map::HexGrid& /*grid*/,
                            PlayerId player) {
    aoc::ecs::World& world = gameState.legacyWorld();
    // AI prioritizes railway construction between capital and other cities
    // This is handled by the builder AI -- here we just set the priority
    const aoc::ecs::ComponentPool<PlayerIndustrialComponent>* indPool =
        world.getPool<PlayerIndustrialComponent>();
    if (indPool == nullptr) { return; }

    for (uint32_t i = 0; i < indPool->size(); ++i) {
        if (indPool->data()[i].owner == player) {
            if (indPool->data()[i].hasRailways()) {
                LOG_INFO("AI player %u: railway construction priority active",
                         static_cast<unsigned>(player));
            }
            break;
        }
    }
}

// ============================================================================
// Crisis response
// ============================================================================

void aiCrisisResponse(aoc::game::GameState& gameState, PlayerId player) {
    aoc::ecs::ComponentPool<CurrencyCrisisComponent>* crisisPool =
        world.getPool<CurrencyCrisisComponent>();
    if (crisisPool == nullptr) { return; }
    aoc::ecs::World& world = gameState.legacyWorld();

    MonetaryStateComponent* myState = findMonetary(world, player);
    if (myState == nullptr) { return; }

    for (uint32_t i = 0; i < crisisPool->size(); ++i) {
        CurrencyCrisisComponent& crisis = crisisPool->data()[i];
        if (crisis.owner != player || crisis.activeCrisis == CrisisType::None) {
            continue;
        }

        switch (crisis.activeCrisis) {
            case CrisisType::BankRun:
                // Raise taxes, cut spending
                myState->taxRate = std::min(0.40f, myState->taxRate + 0.05f);
                myState->governmentSpending = myState->governmentSpending * 3 / 4;
                LOG_INFO("AI player %u: crisis response - raising taxes, cutting spending",
                         static_cast<unsigned>(player));
                break;

            case CrisisType::Hyperinflation:
                // Raise interest rates to maximum
                setInterestRate(*myState, 0.25f);
                myState->governmentSpending = 0;
                LOG_INFO("AI player %u: crisis response - max interest rates, zero spending",
                         static_cast<unsigned>(player));
                break;

            case CrisisType::SovereignDefault:
                // Cut spending to minimum, raise taxes
                myState->governmentSpending = 0;
                myState->taxRate = std::min(0.50f, myState->taxRate + 0.10f);
                LOG_INFO("AI player %u: crisis response - austerity measures",
                         static_cast<unsigned>(player));
                break;

            default:
                break;
        }
        break;
    }
}

// ============================================================================
// Industrial revolution preparation
// ============================================================================

void aiPrepareIndustrialRevolution(aoc::game::GameState& gameState,
                                    const Market& /*market*/,
                                    PlayerId player) {
    aoc::ecs::World& world = gameState.legacyWorld();
    // Check what the next revolution requires and prioritize those techs/resources
    const aoc::ecs::ComponentPool<PlayerIndustrialComponent>* indPool =
        world.getPool<PlayerIndustrialComponent>();
    if (indPool == nullptr) { return; }

    for (uint32_t i = 0; i < indPool->size(); ++i) {
        if (indPool->data()[i].owner != player) { continue; }

        uint8_t nextRev = static_cast<uint8_t>(indPool->data()[i].currentRevolution) + 1;
        if (nextRev > 5) { return; }

        const RevolutionDef& rev = REVOLUTION_DEFS[nextRev - 1];

        // Check if we have the required techs
        const aoc::ecs::ComponentPool<PlayerTechComponent>* techPool =
            world.getPool<PlayerTechComponent>();
        if (techPool == nullptr) { return; }

        for (uint32_t t = 0; t < techPool->size(); ++t) {
            if (techPool->data()[t].owner != player) { continue; }

            // If we're not researching a required tech, suggest it
            for (int32_t r = 0; r < 3; ++r) {
                TechId reqTech = rev.requirements.requiredTechs[r];
                if (reqTech.isValid() && !techPool->data()[t].hasResearched(reqTech)) {
                    // This tech should be prioritized -- logged for now
                    LOG_INFO("AI player %u: should prioritize tech %u for %.*s",
                             static_cast<unsigned>(player),
                             static_cast<unsigned>(reqTech.value),
                             static_cast<int>(rev.name.size()), rev.name.data());
                }
            }
            break;
        }
        break;
    }
}

// ============================================================================
// Gold spending: prevent treasury runaway
// ============================================================================

static void aiSpendExcessGold(aoc::game::GameState& gameState, PlayerId player) {
    MonetaryStateComponent* myState = findMonetary(world, player);
    if (myState == nullptr) { return; }

    // Cap treasury to prevent integer overflow.
    // Excess gold is "spent" on public works (not tracked individually).
    constexpr CurrencyAmount MAX_TREASURY = 50000;
    if (myState->treasury > MAX_TREASURY) {
        myState->treasury = MAX_TREASURY;
    }
}

// ============================================================================
// Master economic strategy
// ============================================================================

void aiEconomicStrategy(aoc::game::GameState& gameState,
                        aoc::map::HexGrid& grid,
                        const Market& market,
                        DiplomacyManager& diplomacy,
                        PlayerId player,
                        int32_t difficulty) {
    aoc::ecs::World& world = gameState.legacyWorld();
    // Run all sub-strategies
    aiBondStrategy(world, player, difficulty);
    aiSanctionStrategy(world, diplomacy, player, difficulty);
    aiInsuranceStrategy(world, player);
    aiImmigrationPolicy(world, player);
    aiManagePowerGrid(world, grid, player);
    aiManageInfrastructure(world, grid, player);
    aiCrisisResponse(world, player);
    aiSpendExcessGold(world, player);
    aiPrepareIndustrialRevolution(world, market, player);
}

} // namespace aoc::sim
