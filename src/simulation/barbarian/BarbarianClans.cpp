/**
 * @file BarbarianClans.cpp
 * @brief Barbarian clan interactions and difficulty scaling.
 */

#include "aoc/simulation/city/District.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/citystate/CityState.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/simulation/barbarian/BarbarianClans.hpp"
#include "aoc/simulation/barbarian/BarbarianController.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
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
    gsPlayer->addGold(-static_cast<CurrencyAmount>(cost), aoc::sim::MoneyFlow::external());

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
    hirerPlayer->addGold(-static_cast<CurrencyAmount>(cost), aoc::sim::MoneyFlow::external());

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
    // Ids are the melee line of UNIT_TYPE_DEFS (UnitTypes.hpp); until
    // 2026-09-05 two rows named ids that do not exist (13) or the wrong unit
    // (11 is the Crossbowman), and the two ladders were compared by raw id.
    UnitTypeId turnUnit = UnitTypeId{0};
    if      (turnNumber < 30)  { turnUnit = UnitTypeId{0}; }  // Warrior
    else if (turnNumber < 60)  { turnUnit = UnitTypeId{9}; }  // Spearman
    else if (turnNumber < 100) { turnUnit = UnitTypeId{10}; } // Swordsman
    else if (turnNumber < 150) { turnUnit = UnitTypeId{34}; } // Musketman
    else if (turnNumber < 200) { turnUnit = UnitTypeId{15}; } // Infantry
    else                       { turnUnit = UnitTypeId{17}; } // Tank

    if (leadingEra < 0) { return turnUnit; }

    UnitTypeId eraUnit = UnitTypeId{0};
    switch (leadingEra) {
        case 0: eraUnit = UnitTypeId{0};  break; // Ancient     : Warrior
        case 1: eraUnit = UnitTypeId{10}; break; // Classical   : Swordsman
        case 2: eraUnit = UnitTypeId{33}; break; // Medieval    : Man-at-Arms
        case 3: eraUnit = UnitTypeId{34}; break; // Renaissance : Musketman
        case 4: eraUnit = UnitTypeId{15}; break; // Industrial  : Infantry
        case 5: eraUnit = UnitTypeId{17}; break; // Modern      : Tank
        default: eraUnit = UnitTypeId{17}; break; // Information+: Tank
    }
    const int32_t eraStrength  = unitTypeDef(eraUnit).combatStrength;
    const int32_t turnStrength = unitTypeDef(turnUnit).combatStrength;
    return (eraStrength > turnStrength) ? eraUnit : turnUnit;
}

int32_t encampmentDestroyReward(int32_t clanStrength) {
    return 25 + clanStrength * 15;  // 25 base + 15 per strength level
}


ErrorCode convertClanToCityState(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                                 std::size_t clanIndex, hex::AxialCoord campLocation,
                                 PlayerId player) {
    std::vector<BarbarianClanComponent>& clans = gameState.barbarianClans();
    if (clanIndex >= clans.size()) {
        return ErrorCode::InvalidArgument;
    }
    BarbarianClanComponent& clan = clans[clanIndex];

    aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) {
        return ErrorCode::InvalidArgument;
    }
    const int32_t cost = convertToCityStateCost(clan.strength);
    if (gsPlayer->monetary().treasury < static_cast<CurrencyAmount>(cost)) {
        return ErrorCode::InsufficientResources;
    }
    if (!grid.isValid(campLocation)) {
        return ErrorCode::InvalidArgument;
    }

    // A free city-state seat is needed to settle them into.
    const std::size_t seat = gameState.cityStates().size();
    aoc::game::Player* csPlayer =
        gameState.player(static_cast<PlayerId>(CITY_STATE_PLAYER_BASE + seat));
    if (csPlayer == nullptr) {
        return ErrorCode::InvalidState; // no seat left to settle them into
    }

    gsPlayer->addGold(-static_cast<CurrencyAmount>(cost), aoc::sim::MoneyFlow::external());

    CityStateComponent cs{};
    cs.defId    = clan.clanId;
    cs.type     = CityStateType::Militaristic; // they were a warband yesterday
    cs.location = campLocation;
    cs.envoys.fill(0);
    // The point of paying is the ally it leaves you with.
    cs.suzerain = player;
    if (player < MAX_PLAYERS) {
        cs.metMask |= (1u << player);
    }
    gameState.cityStates().push_back(cs);

    aoc::game::City& city =
        csPlayer->addCity(campLocation, std::string(BARBARIAN_CLAN_DEFS[clan.clanId].name));
    city.setPopulation(2);
    CityDistrictsComponent::PlacedDistrict center;
    center.type     = DistrictType::CityCenter;
    center.location = campLocation;
    city.districts().districts.push_back(std::move(center));

    LOG_INFO("Player %u settled the %.*s as a city-state for %d gold",
             static_cast<unsigned>(player),
             static_cast<int>(BARBARIAN_CLAN_DEFS[clan.clanId].name.size()),
             BARBARIAN_CLAN_DEFS[clan.clanId].name.data(), cost);

    // They are no longer barbarians.
    clan.isBribed       = false;
    clan.hiredBy        = INVALID_PLAYER;
    clan.hiredTarget    = INVALID_PLAYER;
    clan.strength       = 0;
    return ErrorCode::Ok;
}

} // namespace aoc::sim
