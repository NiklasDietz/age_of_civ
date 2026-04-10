/**
 * @file EconomicEspionage.cpp
 * @brief Economic espionage mission execution.
 */

#include "aoc/simulation/economy/EconomicEspionage.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

EspionageResult executeEspionageMission(aoc::ecs::World& world,
                                         PlayerId spyOwner,
                                         PlayerId target,
                                         EspionageMissionType mission,
                                         float spySkill,
                                         uint32_t rngSeed) {
    EspionageResult result{};

    // Success roll
    float baseRate = espionageBaseSuccessRate(mission);
    float totalRate = baseRate + spySkill * 0.30f;  // Skill adds up to 30%
    totalRate = std::clamp(totalRate, 0.10f, 0.90f);

    uint32_t hash = rngSeed * 2654435761u;
    float roll = static_cast<float>(hash % 10000u) / 10000.0f;

    result.succeeded = (roll < totalRate);

    // Detection roll (even on success, spy might be caught)
    uint32_t detectHash = (rngSeed + 1u) * 2246822519u;
    float detectRoll = static_cast<float>(detectHash % 10000u) / 10000.0f;
    float detectChance = 0.20f - spySkill * 0.10f;  // 10-20% base detection
    if (!result.succeeded) { detectChance += 0.20f; }  // Higher if mission failed
    result.spyCaught = (detectRoll < detectChance);

    if (!result.succeeded) {
        LOG_INFO("Espionage: player %u's %.*s mission against player %u FAILED%s",
                 static_cast<unsigned>(spyOwner),
                 static_cast<int>(espionageMissionName(mission).size()),
                 espionageMissionName(mission).data(),
                 static_cast<unsigned>(target),
                 result.spyCaught ? " (SPY CAUGHT)" : "");
        return result;
    }

    // Apply mission effects
    aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();

    switch (mission) {
        case EspionageMissionType::StealTradeSecrets: {
            // Gain 25% of a random unresearched tech's cost
            aoc::ecs::ComponentPool<PlayerTechComponent>* techPool =
                world.getPool<PlayerTechComponent>();
            if (techPool != nullptr) {
                for (uint32_t i = 0; i < techPool->size(); ++i) {
                    if (techPool->data()[i].owner == spyOwner) {
                        // Find current research and boost it
                        TechId currentResearch = techPool->data()[i].currentResearch;
                        if (currentResearch.isValid() && currentResearch.value < techCount()) {
                            float cost = static_cast<float>(techDef(currentResearch).researchCost);
                            result.techProgressGained = cost * 0.25f;
                            techPool->data()[i].researchProgress += result.techProgressGained;
                        }
                        break;
                    }
                }
            }
            break;
        }

        case EspionageMissionType::CounterfeitCurrency: {
            // Inject 10% of target's money supply as counterfeit
            if (monetaryPool != nullptr) {
                for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
                    if (monetaryPool->data()[i].owner == target) {
                        MonetaryStateComponent& targetState = monetaryPool->data()[i];
                        CurrencyAmount fake = static_cast<CurrencyAmount>(
                            static_cast<float>(targetState.moneySupply) * 0.10f);
                        targetState.moneySupply += fake;
                        result.inflationInjected = 0.10f;
                        break;
                    }
                }
            }
            break;
        }

        case EspionageMissionType::IndustrialSabotage: {
            // Destroy a random building in a target city
            aoc::ecs::ComponentPool<CityComponent>* cityPool =
                world.getPool<CityComponent>();
            if (cityPool != nullptr) {
                for (uint32_t i = 0; i < cityPool->size(); ++i) {
                    if (cityPool->data()[i].owner != target) { continue; }
                    EntityId cityEntity = cityPool->entities()[i];
                    CityDistrictsComponent* districts =
                        world.tryGetComponent<CityDistrictsComponent>(cityEntity);
                    if (districts == nullptr) { continue; }

                    // Find and remove one building
                    for (CityDistrictsComponent::PlacedDistrict& district : districts->districts) {
                        if (!district.buildings.empty()) {
                            district.buildings.pop_back();
                            result.buildingDestroyed = true;
                            break;
                        }
                    }
                    if (result.buildingDestroyed) { break; }
                }
            }
            break;
        }

        case EspionageMissionType::InsiderTrading: {
            // Gain 5% of target's GDP as gold
            if (monetaryPool != nullptr) {
                for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
                    if (monetaryPool->data()[i].owner == target) {
                        result.goldGained = static_cast<CurrencyAmount>(
                            static_cast<float>(monetaryPool->data()[i].gdp) * 0.05f);
                        break;
                    }
                }
                // Pay the spy owner
                for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
                    if (monetaryPool->data()[i].owner == spyOwner) {
                        monetaryPool->data()[i].treasury += result.goldGained;
                        break;
                    }
                }
            }
            break;
        }

        case EspionageMissionType::EmbargoIntelligence: {
            // Intelligence gathering: no direct effect, enables diplomatic actions
            // The caller should reveal trade routes to the spy owner's UI
            break;
        }

        default:
            break;
    }

    LOG_INFO("Espionage: player %u's %.*s mission against player %u SUCCEEDED%s",
             static_cast<unsigned>(spyOwner),
             static_cast<int>(espionageMissionName(mission).size()),
             espionageMissionName(mission).data(),
             static_cast<unsigned>(target),
             result.spyCaught ? " (but SPY CAUGHT)" : "");

    return result;
}

} // namespace aoc::sim
