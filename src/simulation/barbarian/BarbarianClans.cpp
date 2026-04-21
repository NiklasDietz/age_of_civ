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

UnitTypeId barbarianSpawnUnit(int32_t turnNumber, int32_t leadingEra) {
    // H5.7: era floor. Without it, a sprinting player can reach Tanks by turn
    // 130 while barbs are still frozen on Musketmen until turn 200.
    UnitTypeId turnUnit = UnitTypeId{0};
    if      (turnNumber < 30)  { turnUnit = UnitTypeId{0}; }  // Warrior
    else if (turnNumber < 60)  { turnUnit = UnitTypeId{9}; }  // Spearman
    else if (turnNumber < 100) { turnUnit = UnitTypeId{10}; } // Swordsman
    else if (turnNumber < 150) { turnUnit = UnitTypeId{13}; } // Musketman
    else if (turnNumber < 200) { turnUnit = UnitTypeId{15}; } // Infantry
    else                       { turnUnit = UnitTypeId{17}; } // Tank

    if (leadingEra < 0) { return turnUnit; }

    UnitTypeId eraUnit = UnitTypeId{0};
    switch (leadingEra) {
        case 0: eraUnit = UnitTypeId{0};  break; // Ancient     : Warrior
        case 1: eraUnit = UnitTypeId{10}; break; // Classical   : Swordsman
        case 2: eraUnit = UnitTypeId{11}; break; // Medieval    : Knight-class (Horsemen id 11)
        case 3: eraUnit = UnitTypeId{13}; break; // Renaissance : Musketman
        case 4: eraUnit = UnitTypeId{15}; break; // Industrial  : Infantry
        case 5: eraUnit = UnitTypeId{17}; break; // Modern      : Tank
        default: eraUnit = UnitTypeId{17}; break; // Information+: Tank
    }
    return (eraUnit.value > turnUnit.value) ? eraUnit : turnUnit;
}

int32_t encampmentDestroyReward(int32_t clanStrength) {
    return 25 + clanStrength * 15;  // 25 base + 15 per strength level
}

} // namespace aoc::sim
