/**
 * @file BarbarianClans.cpp
 * @brief Barbarian clan interactions and difficulty scaling.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/simulation/barbarian/BarbarianClans.hpp"
#include "aoc/simulation/barbarian/BarbarianController.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/core/Log.hpp"

#include "aoc/game/Player.hpp"

namespace aoc::sim {

ErrorCode bribeClan(aoc::game::GameState& gameState,
                    std::size_t clanIndex,
                    PlayerId player) {
    std::vector<BarbarianClanComponent>& clans = gameState.barbarianClans();
    if (clanIndex >= clans.size()) {
        return ErrorCode::InvalidArgument;
    }
    BarbarianClanComponent& clan = clans[clanIndex];
    if (clan.isBribed) {
        return ErrorCode::InvalidArgument;
    }

    const int32_t cost = bribeCost(clan.strength);

    aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) {
        return ErrorCode::InvalidArgument;
    }
    if (gsPlayer->monetary().treasury < static_cast<CurrencyAmount>(cost)) {
        return ErrorCode::InsufficientResources;
    }
    gsPlayer->monetary().treasury -= static_cast<CurrencyAmount>(cost);

    clan.isBribed = true;
    clan.bribeTurnsLeft = 20;
    LOG_INFO("Player %u bribed %.*s for %d gold",
             static_cast<unsigned>(player),
             static_cast<int>(BARBARIAN_CLAN_DEFS[clan.clanId].name.size()),
             BARBARIAN_CLAN_DEFS[clan.clanId].name.data(), cost);

    return ErrorCode::Ok;
}

ErrorCode hireClan(aoc::game::GameState& gameState,
                   std::size_t clanIndex,
                   PlayerId hirer,
                   PlayerId target) {
    std::vector<BarbarianClanComponent>& clans = gameState.barbarianClans();
    if (clanIndex >= clans.size()) {
        return ErrorCode::InvalidArgument;
    }
    BarbarianClanComponent& clan = clans[clanIndex];

    const int32_t cost = hireCost(clan.strength);

    aoc::game::Player* hirerPlayer = gameState.player(hirer);
    if (hirerPlayer == nullptr) {
        return ErrorCode::InvalidArgument;
    }
    if (hirerPlayer->monetary().treasury < static_cast<CurrencyAmount>(cost)) {
        return ErrorCode::InsufficientResources;
    }
    hirerPlayer->monetary().treasury -= static_cast<CurrencyAmount>(cost);

    clan.hiredBy = hirer;
    clan.hiredTarget = target;
    clan.hireTurnsLeft = 10;
    clan.isBribed = false;  // Not passive if hired
    LOG_INFO("Player %u hired %.*s to attack player %u",
             static_cast<unsigned>(hirer),
             static_cast<int>(BARBARIAN_CLAN_DEFS[clan.clanId].name.size()),
             BARBARIAN_CLAN_DEFS[clan.clanId].name.data(),
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
