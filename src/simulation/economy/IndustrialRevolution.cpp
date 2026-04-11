/**
 * @file IndustrialRevolution.cpp
 * @brief Industrial revolution detection and progression.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/simulation/economy/IndustrialRevolution.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

namespace aoc::sim {

bool checkIndustrialRevolution(aoc::game::GameState& gameState, PlayerId player,
                               TurnNumber currentTurn) {
    aoc::ecs::World& world = gameState.legacyWorld();
    // Find or create the player's industrial component
    aoc::ecs::ComponentPool<PlayerIndustrialComponent>* indPool =
        world.getPool<PlayerIndustrialComponent>();

    PlayerIndustrialComponent* ind = nullptr;
    EntityId indEntity = NULL_ENTITY;
    if (indPool != nullptr) {
        for (uint32_t i = 0; i < indPool->size(); ++i) {
            if (indPool->data()[i].owner == player) {
                ind = &indPool->data()[i];
                indEntity = indPool->entities()[i];
                break;
            }
        }
    }

    if (ind == nullptr) {
        return false;  // No industrial component yet
    }

    // Determine the next revolution to check
    uint8_t nextRevId = static_cast<uint8_t>(ind->currentRevolution) + 1;
    if (nextRevId > static_cast<uint8_t>(IndustrialRevolutionId::Fifth)) {
        return false;  // Already at max
    }

    const RevolutionDef& rev = REVOLUTION_DEFS[nextRevId - 1];

    // Check tech requirements
    const aoc::ecs::ComponentPool<PlayerTechComponent>* techPool =
        world.getPool<PlayerTechComponent>();
    if (techPool == nullptr) {
        return false;
    }

    const PlayerTechComponent* playerTech = nullptr;
    for (uint32_t t = 0; t < techPool->size(); ++t) {
        if (techPool->data()[t].owner == player) {
            playerTech = &techPool->data()[t];
            break;
        }
    }
    if (playerTech == nullptr) {
        return false;
    }

    for (int32_t i = 0; i < 3; ++i) {
        TechId reqTech = rev.requirements.requiredTechs[i];
        if (reqTech.isValid() && !playerTech->hasResearched(reqTech)) {
            return false;
        }
    }

    // Check resource requirements (player must have these goods in any city stockpile)
    const aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    const aoc::ecs::ComponentPool<CityStockpileComponent>* stockpilePool =
        world.getPool<CityStockpileComponent>();

    // Count cities
    int32_t cityCount = 0;
    if (cityPool != nullptr) {
        for (uint32_t c = 0; c < cityPool->size(); ++c) {
            if (cityPool->data()[c].owner == player) {
                ++cityCount;
            }
        }
    }

    if (cityCount < rev.requirements.minCityCount) {
        return false;
    }

    // Check resource availability
    for (int32_t i = 0; i < 3; ++i) {
        uint16_t reqGood = rev.requirements.requiredGoods[i];
        if (reqGood == 0xFFFF) {
            continue;
        }

        bool found = false;
        if (cityPool != nullptr && stockpilePool != nullptr) {
            for (uint32_t c = 0; c < cityPool->size(); ++c) {
                if (cityPool->data()[c].owner != player) {
                    continue;
                }
                EntityId cityEntity = cityPool->entities()[c];
                const CityStockpileComponent* stockpile =
                    world.tryGetComponent<CityStockpileComponent>(cityEntity);
                if (stockpile != nullptr && stockpile->getAmount(reqGood) > 0) {
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            return false;
        }
    }

    // All requirements met -- advance!
    ind->currentRevolution = static_cast<IndustrialRevolutionId>(nextRevId);
    ind->turnAchieved[nextRevId] = static_cast<int32_t>(currentTurn);

    LOG_INFO("Player %u achieved the %.*s (Industrial Revolution #%u) on turn %d!",
             static_cast<unsigned>(player),
             static_cast<int>(rev.name.size()), rev.name.data(),
             static_cast<unsigned>(nextRevId),
             static_cast<int>(currentTurn));

    return true;
}

float revolutionPollutionMultiplier(const PlayerIndustrialComponent& ind) {
    float mult = 1.0f;
    for (uint8_t r = 1; r <= static_cast<uint8_t>(ind.currentRevolution); ++r) {
        mult *= REVOLUTION_DEFS[r - 1].bonuses.pollutionMultiplier;
    }
    return mult;
}

} // namespace aoc::sim
