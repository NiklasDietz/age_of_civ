/**
 * @file BarbarianClans.cpp
 * @brief Barbarian clan interactions and difficulty scaling.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/simulation/barbarian/BarbarianClans.hpp"
#include "aoc/simulation/barbarian/BarbarianController.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

namespace aoc::sim {

ErrorCode bribeClan(aoc::game::GameState& gameState, EntityId encampmentEntity, PlayerId player) {
    BarbarianClanComponent* clan = world.tryGetComponent<BarbarianClanComponent>(encampmentEntity);
    if (clan == nullptr || clan->isBribed) {
        return ErrorCode::InvalidArgument;
    }

    int32_t cost = bribeCost(clan->strength);

    // Deduct gold from player treasury
    aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();
    if (monetaryPool != nullptr) {
        for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
            if (monetaryPool->data()[i].owner == player) {
                if (monetaryPool->data()[i].treasury < static_cast<CurrencyAmount>(cost)) {
                    return ErrorCode::InsufficientResources;
                }
                monetaryPool->data()[i].treasury -= static_cast<CurrencyAmount>(cost);
                break;
            }
        }
    }

    clan->isBribed = true;
    clan->bribeTurnsLeft = 20;
    LOG_INFO("Player %u bribed %.*s for %d gold",
             static_cast<unsigned>(player),
             static_cast<int>(BARBARIAN_CLAN_DEFS[clan->clanId].name.size()),
             BARBARIAN_CLAN_DEFS[clan->clanId].name.data(), cost);

    return ErrorCode::Ok;
}

ErrorCode hireClan(aoc::game::GameState& gameState, EntityId encampmentEntity,
                    PlayerId hirer, PlayerId target) {
    aoc::ecs::World& world = gameState.legacyWorld();
    BarbarianClanComponent* clan = world.tryGetComponent<BarbarianClanComponent>(encampmentEntity);
    if (clan == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    int32_t cost = hireCost(clan->strength);

    aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();
    if (monetaryPool != nullptr) {
        for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
            if (monetaryPool->data()[i].owner == hirer) {
                if (monetaryPool->data()[i].treasury < static_cast<CurrencyAmount>(cost)) {
                    return ErrorCode::InsufficientResources;
                }
                monetaryPool->data()[i].treasury -= static_cast<CurrencyAmount>(cost);
                break;
            }
        }
    }

    clan->hiredBy = hirer;
    clan->hiredTarget = target;
    clan->hireTurnsLeft = 10;
    clan->isBribed = false;  // Not passive if hired
    LOG_INFO("Player %u hired %.*s to attack player %u",
             static_cast<unsigned>(hirer),
             static_cast<int>(BARBARIAN_CLAN_DEFS[clan->clanId].name.size()),
             BARBARIAN_CLAN_DEFS[clan->clanId].name.data(),
             static_cast<unsigned>(target));

    return ErrorCode::Ok;
}

UnitTypeId barbarianSpawnUnit(int32_t turnNumber) {
    if (turnNumber < 30)  { return UnitTypeId{0}; }  // Warrior
    if (turnNumber < 60)  { return UnitTypeId{9}; }  // Spearman
    if (turnNumber < 100) { return UnitTypeId{10}; } // Swordsman
    if (turnNumber < 150) { return UnitTypeId{13}; } // Musketman
    if (turnNumber < 200) { return UnitTypeId{15}; } // Infantry
    return UnitTypeId{17};                            // Tank
}

int32_t encampmentDestroyReward(int32_t clanStrength) {
    return 25 + clanStrength * 15;  // 25 base + 15 per strength level
}

} // namespace aoc::sim
