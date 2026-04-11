/**
 * @file TheologicalCombat.cpp
 * @brief Theological combat resolution between religious units.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/simulation/religion/TheologicalCombat.hpp"
#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim {

ErrorCode resolveTheologicalCombat(aoc::game::GameState& gameState,
                                    const aoc::map::HexGrid& /*grid*/,
                                    EntityId attackerEntity,
                                    EntityId defenderEntity) {
    aoc::ecs::World& world = gameState.legacyWorld();
    UnitComponent* attacker = world.tryGetComponent<UnitComponent>(attackerEntity);
    UnitComponent* defender = world.tryGetComponent<UnitComponent>(defenderEntity);
    if (attacker == nullptr || defender == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    // Both must be religious units
    if (!isReligious(unitTypeDef(attacker->typeId).unitClass)
        || !isReligious(unitTypeDef(defender->typeId).unitClass)) {
        return ErrorCode::InvalidArgument;
    }

    int32_t atkStr = religiousCombatStrength(attacker->typeId);
    int32_t defStr = religiousCombatStrength(defender->typeId);

    if (atkStr <= 0 || defStr <= 0) {
        return ErrorCode::InvalidArgument;
    }

    // Health modifier
    float atkHP = static_cast<float>(attacker->hitPoints) / static_cast<float>(unitTypeDef(attacker->typeId).maxHitPoints);
    float defHP = static_cast<float>(defender->hitPoints) / static_cast<float>(unitTypeDef(defender->typeId).maxHitPoints);

    float atkPower = static_cast<float>(atkStr) * atkHP;
    float defPower = static_cast<float>(defStr) * defHP;

    // Damage calculation (simplified Lanchester)
    float ratio = atkPower / std::max(0.01f, defPower);
    int32_t atkDamage = static_cast<int32_t>(30.0f / std::max(0.5f, ratio));
    int32_t defDamage = static_cast<int32_t>(30.0f * std::min(2.0f, ratio));

    attacker->hitPoints -= atkDamage;
    defender->hitPoints -= defDamage;

    LOG_INFO("Theological combat: %.*s (%d HP) vs %.*s (%d HP)",
             static_cast<int>(unitTypeDef(attacker->typeId).name.size()),
             unitTypeDef(attacker->typeId).name.data(),
             attacker->hitPoints,
             static_cast<int>(unitTypeDef(defender->typeId).name.size()),
             unitTypeDef(defender->typeId).name.data(),
             defender->hitPoints);

    // Destroy dead units
    if (defender->hitPoints <= 0) {
        world.destroyEntity(defenderEntity);
    }
    if (attacker->hitPoints <= 0) {
        world.destroyEntity(attackerEntity);
    }

    return ErrorCode::Ok;
}

ErrorCode purgeReligion(aoc::game::GameState& gameState,
                         EntityId inquisitorEntity,
                         EntityId cityEntity) {
    aoc::ecs::World& world = gameState.legacyWorld();
    UnitComponent* inquisitor = world.tryGetComponent<UnitComponent>(inquisitorEntity);
    if (inquisitor == nullptr || inquisitor->spreadCharges <= 0) {
        return ErrorCode::InvalidArgument;
    }

    CityReligionComponent* cityRel = world.tryGetComponent<CityReligionComponent>(cityEntity);
    if (cityRel == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    // Remove all foreign religious pressure (keep only the inquisitor's religion)
    uint8_t inquisitorReligion = inquisitor->spreadingReligion;
    for (uint8_t r = 0; r < MAX_RELIGIONS; ++r) {
        if (r != inquisitorReligion) {
            cityRel->pressure[r] = 0.0f;
        }
    }

    --inquisitor->spreadCharges;
    if (inquisitor->spreadCharges <= 0) {
        world.destroyEntity(inquisitorEntity);
    }

    LOG_INFO("Inquisitor purged foreign religions from city");

    return ErrorCode::Ok;
}

} // namespace aoc::sim
