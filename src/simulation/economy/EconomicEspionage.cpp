/**
 * @file EconomicEspionage.cpp
 * @brief Economic espionage mission execution.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/economy/EconomicEspionage.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

EspionageResult executeEspionageMission(aoc::game::GameState& gameState,
                                         PlayerId spyOwner,
                                         PlayerId target,
                                         EspionageMissionType mission,
                                         float spySkill,
                                         uint32_t rngSeed) {
    EspionageResult result{};

    float baseRate  = espionageBaseSuccessRate(mission);
    float totalRate = baseRate + spySkill * 0.30f;
    totalRate = std::clamp(totalRate, 0.10f, 0.90f);

    uint32_t hash = rngSeed * 2654435761u;
    float roll    = static_cast<float>(hash % 10000u) / 10000.0f;
    result.succeeded = (roll < totalRate);

    uint32_t detectHash  = (rngSeed + 1u) * 2246822519u;
    float detectRoll     = static_cast<float>(detectHash % 10000u) / 10000.0f;
    float detectChance   = 0.20f - spySkill * 0.10f;
    if (!result.succeeded) { detectChance += 0.20f; }
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

    aoc::game::Player* spyPlayer    = gameState.player(spyOwner);
    aoc::game::Player* targetPlayer = gameState.player(target);

    switch (mission) {
        case EspionageMissionType::StealTradeSecrets: {
            if (spyPlayer != nullptr) {
                PlayerTechComponent& tech = spyPlayer->tech();
                TechId currentResearch    = tech.currentResearch;
                if (currentResearch.isValid() && currentResearch.value < techCount()) {
                    float cost = static_cast<float>(techDef(currentResearch).researchCost);
                    result.techProgressGained  = cost * 0.25f;
                    tech.researchProgress     += result.techProgressGained;
                }
            }
            break;
        }

        case EspionageMissionType::CounterfeitCurrency: {
            if (targetPlayer != nullptr) {
                MonetaryStateComponent& targetState = targetPlayer->monetary();
                CurrencyAmount fake = static_cast<CurrencyAmount>(
                    static_cast<float>(targetState.moneySupply) * 0.10f);
                targetState.moneySupply  += fake;
                result.inflationInjected  = 0.10f;
            }
            break;
        }

        case EspionageMissionType::IndustrialSabotage: {
            if (targetPlayer != nullptr) {
                for (const std::unique_ptr<aoc::game::City>& cityPtr : targetPlayer->cities()) {
                    if (cityPtr == nullptr) { continue; }
                    CityDistrictsComponent& districts = cityPtr->districts();
                    for (CityDistrictsComponent::PlacedDistrict& district : districts.districts) {
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
            if (targetPlayer != nullptr) {
                result.goldGained = static_cast<CurrencyAmount>(
                    static_cast<float>(targetPlayer->monetary().gdp) * 0.05f);
            }
            if (spyPlayer != nullptr) {
                spyPlayer->monetary().treasury += result.goldGained;
            }
            break;
        }

        case EspionageMissionType::EmbargoIntelligence:
            // Intelligence gathering: caller reveals trade routes to spyOwner's UI
            break;

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
