/**
 * @file UnitUpgrade.cpp
 * @brief Unit upgrade path logic: checks, costs, and execution.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/unit/UnitUpgrade.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <array>

namespace aoc::sim {

namespace {

/// Static upgrade path table.
struct UpgradePathEntry {
    uint16_t fromValue;
    uint16_t toValue;
    uint16_t requiredTechValue;
};

constexpr std::array<UpgradePathEntry, 8> UPGRADE_PATHS = {{
    { 0, 10,  8},  // Warrior -> Swordsman, requires Metallurgy
    { 1, 11,  7},  // Slinger -> Crossbowman, requires Apprenticeship
    { 9, 13, 10},  // Spearman -> Musketman, requires Gunpowder
    {10, 15, 11},  // Swordsman -> Infantry, requires Industrialization
    {11, 16, 14},  // Crossbowman -> Artillery, requires Electricity
    {12, 14, 11},  // Knight -> Cavalry, requires Industrialization
    {14, 17, 15},  // Cavalry -> Tank, requires Mass Production
    { 4, 12,  8},  // Horseman -> Knight, requires Metallurgy
}};

} // anonymous namespace

std::vector<UnitUpgradeDef> getAvailableUpgrades(UnitTypeId currentType) {
    std::vector<UnitUpgradeDef> result;
    for (const UpgradePathEntry& entry : UPGRADE_PATHS) {
        if (entry.fromValue == currentType.value) {
            UnitUpgradeDef def{};
            def.from = UnitTypeId{entry.fromValue};
            def.to = UnitTypeId{entry.toValue};
            def.requiredTech = TechId{entry.requiredTechValue};
            result.push_back(def);
        }
    }
    return result;
}

int32_t upgradeCost(UnitTypeId from, UnitTypeId to) {
    const UnitTypeDef& fromDef = unitTypeDef(from);
    const UnitTypeDef& toDef = unitTypeDef(to);
    const int32_t cost = (toDef.productionCost - fromDef.productionCost) * 2;
    return std::max(cost, 20);
}

bool upgradeUnit(aoc::game::GameState& gameState, aoc::game::Unit& unit,
                  UnitTypeId newType, PlayerId player) {
    aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) {
        LOG_ERROR("upgradeUnit: player not found");
        return false;
    }

    // Check that a valid upgrade path exists
    const std::vector<UnitUpgradeDef> upgrades = getAvailableUpgrades(unit.typeId());
    const UnitUpgradeDef* matchedUpgrade = nullptr;
    for (const UnitUpgradeDef& upg : upgrades) {
        if (upg.to == newType) {
            matchedUpgrade = &upg;
            break;
        }
    }
    if (matchedUpgrade == nullptr) {
        LOG_ERROR("upgradeUnit: no upgrade path from %u to %u",
                  static_cast<unsigned>(unit.typeId().value),
                  static_cast<unsigned>(newType.value));
        return false;
    }

    // Check that the required tech is researched
    if (!gsPlayer->tech().hasResearched(matchedUpgrade->requiredTech)) {
        LOG_INFO("upgradeUnit: required tech not researched");
        return false;
    }

    // Check gold cost
    const int32_t cost = upgradeCost(unit.typeId(), newType);
    if (gsPlayer->monetary().treasury < static_cast<CurrencyAmount>(cost)) {
        LOG_INFO("upgradeUnit: insufficient gold (%lld < %d)",
                 static_cast<long long>(gsPlayer->monetary().treasury), cost);
        return false;
    }

    // Perform the upgrade
    const UnitTypeDef& oldDef = unitTypeDef(unit.typeId());
    const UnitTypeDef& newDef = unitTypeDef(newType);

    // Adjust HP proportionally
    const float hpRatio = static_cast<float>(unit.hitPoints()) /
                          static_cast<float>(oldDef.maxHitPoints);
    const int32_t newHp = std::max(1,
        static_cast<int32_t>(hpRatio * static_cast<float>(newDef.maxHitPoints)));

    // Switch the unit's type BEFORE adjusting HP/movement so that subsequent
    // typeDef() lookups (combat strength, unit class, movement refresh) see
    // the upgraded definition. Previously the type was never updated, leaving
    // the unit permanently bound to its old class.
    unit.setTypeId(newType);
    unit.setHitPoints(newHp);
    unit.setMovementRemaining(newDef.movementPoints);

    // Deduct gold
    gsPlayer->monetary().treasury -= static_cast<CurrencyAmount>(cost);

    LOG_INFO("Player %u upgraded %.*s -> %.*s for %d gold",
             static_cast<unsigned>(player),
             static_cast<int>(oldDef.name.size()), oldDef.name.data(),
             static_cast<int>(newDef.name.size()), newDef.name.data(),
             cost);

    return true;
}

} // namespace aoc::sim
