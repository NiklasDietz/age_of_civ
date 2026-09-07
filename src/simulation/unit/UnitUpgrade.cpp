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

std::vector<UnitUpgradeDef> getAvailableUpgrades(UnitTypeId currentType) {
    // UNIT_TYPE_DEFS is the single source of truth. An eight-row UPGRADE_PATHS
    // table used to live here and answer this question instead, so the
    // upgradesTo and upgradeCost columns on all 78 unit rows were dead --
    // Unit::upgradeTarget, canUpgrade and upgradeCost, their only readers, had
    // no callers at all. The two tables also disagreed: this one sent a Slinger
    // to the Archer, UPGRADE_PATHS sent it to the Crossbowman.
    //
    // The tech gate is the successor's own requiredTech. A separate column for
    // it is what let the two tables drift apart in the first place: a unit you
    // cannot build yet is a unit you cannot upgrade into.
    std::vector<UnitUpgradeDef> result;
    if (currentType.value >= UNIT_TYPE_COUNT) {
        return result;
    }
    const UnitTypeId next = unitTypeDef(currentType).upgradesTo;
    if (!next.isValid() || next.value >= UNIT_TYPE_COUNT) {
        return result;
    }
    UnitUpgradeDef def{};
    def.from         = currentType;
    def.to           = next;
    def.requiredTech = unitTypeDef(next).requiredTech;
    result.push_back(def);
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
