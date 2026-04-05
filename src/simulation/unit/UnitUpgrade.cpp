/**
 * @file UnitUpgrade.cpp
 * @brief Unit upgrade path logic: checks, costs, and execution.
 */

#include "aoc/simulation/unit/UnitUpgrade.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/ecs/World.hpp"

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

bool upgradeUnit(aoc::ecs::World& world, EntityId unitEntity,
                  UnitTypeId newType, PlayerId player) {
    // Verify the unit exists
    UnitComponent* unit = world.tryGetComponent<UnitComponent>(unitEntity);
    if (unit == nullptr) {
        LOG_ERROR("upgradeUnit: unit entity not found");
        return false;
    }

    // Check that a valid upgrade path exists
    const std::vector<UnitUpgradeDef> upgrades = getAvailableUpgrades(unit->typeId);
    const UnitUpgradeDef* matchedUpgrade = nullptr;
    for (const UnitUpgradeDef& upg : upgrades) {
        if (upg.to == newType) {
            matchedUpgrade = &upg;
            break;
        }
    }
    if (matchedUpgrade == nullptr) {
        LOG_ERROR("upgradeUnit: no upgrade path from %u to %u",
                  static_cast<unsigned>(unit->typeId.value),
                  static_cast<unsigned>(newType.value));
        return false;
    }

    // Check that the required tech is researched
    bool techResearched = false;
    const aoc::ecs::ComponentPool<PlayerTechComponent>* techPool =
        world.getPool<PlayerTechComponent>();
    if (techPool != nullptr) {
        for (uint32_t i = 0; i < techPool->size(); ++i) {
            if (techPool->data()[i].owner == player) {
                techResearched = techPool->data()[i].hasResearched(matchedUpgrade->requiredTech);
                break;
            }
        }
    }
    if (!techResearched) {
        LOG_INFO("upgradeUnit: required tech not researched");
        return false;
    }

    // Check gold cost
    const int32_t cost = upgradeCost(unit->typeId, newType);
    PlayerEconomyComponent* econ = nullptr;
    const aoc::ecs::ComponentPool<PlayerEconomyComponent>* econPool =
        world.getPool<PlayerEconomyComponent>();
    if (econPool != nullptr) {
        // Need mutable access
        aoc::ecs::ComponentPool<PlayerEconomyComponent>* mutablePool =
            const_cast<aoc::ecs::ComponentPool<PlayerEconomyComponent>*>(econPool);
        for (uint32_t i = 0; i < mutablePool->size(); ++i) {
            if (mutablePool->data()[i].owner == player) {
                econ = &mutablePool->data()[i];
                break;
            }
        }
    }
    if (econ == nullptr || econ->treasury < static_cast<CurrencyAmount>(cost)) {
        LOG_INFO("upgradeUnit: insufficient gold (%lld < %d)",
                 static_cast<long long>(econ != nullptr ? econ->treasury : 0), cost);
        return false;
    }

    // Perform the upgrade
    const UnitTypeDef& oldDef = unitTypeDef(unit->typeId);
    const UnitTypeDef& newDef = unitTypeDef(newType);

    // Adjust HP proportionally
    const float hpRatio = static_cast<float>(unit->hitPoints) /
                          static_cast<float>(oldDef.maxHitPoints);
    unit->typeId = newType;
    unit->hitPoints = static_cast<int32_t>(hpRatio * static_cast<float>(newDef.maxHitPoints));
    unit->hitPoints = std::max(unit->hitPoints, 1);
    unit->movementRemaining = newDef.movementPoints;

    // Deduct gold
    econ->treasury -= static_cast<CurrencyAmount>(cost);

    LOG_INFO("Player %u upgraded %.*s -> %.*s for %d gold",
             static_cast<unsigned>(player),
             static_cast<int>(oldDef.name.size()), oldDef.name.data(),
             static_cast<int>(newDef.name.size()), newDef.name.data(),
             cost);

    return true;
}

} // namespace aoc::sim
