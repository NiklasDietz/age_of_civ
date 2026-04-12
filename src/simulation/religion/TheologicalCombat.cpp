/**
 * @file TheologicalCombat.cpp
 * @brief Theological combat resolution between religious units.
 *
 * Units are looked up through the GameState object model. Hit points and
 * charges are accessed via Unit accessors. The spreading religion is derived
 * from the owning player's founded religion via PlayerFaithComponent.
 * All ECS World access has been removed.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/religion/TheologicalCombat.hpp"
#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim {

namespace {

/**
 * @brief Find a Unit in the GameState by sequential EntityId index.
 *
 * The n-th unit across all players (in player order) maps to EntityId{n}.
 */
aoc::game::Unit* findUnitByEntity(aoc::game::GameState& gameState, EntityId entity) {
    uint32_t remaining = entity.index;
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        const std::vector<std::unique_ptr<aoc::game::Unit>>& units = playerPtr->units();
        const uint32_t count = static_cast<uint32_t>(units.size());
        if (remaining < count) {
            return units[static_cast<std::size_t>(remaining)].get();
        }
        remaining -= count;
    }
    return nullptr;
}

} // anonymous namespace

ErrorCode resolveTheologicalCombat(aoc::game::GameState& gameState,
                                    const aoc::map::HexGrid& /*grid*/,
                                    EntityId attackerEntity,
                                    EntityId defenderEntity) {
    aoc::game::Unit* attacker = findUnitByEntity(gameState, attackerEntity);
    aoc::game::Unit* defender = findUnitByEntity(gameState, defenderEntity);
    if (attacker == nullptr || defender == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    // Both must be religious units
    if (!isReligious(unitTypeDef(attacker->typeId()).unitClass)
        || !isReligious(unitTypeDef(defender->typeId()).unitClass)) {
        return ErrorCode::InvalidArgument;
    }

    const int32_t atkStr = religiousCombatStrength(attacker->typeId());
    const int32_t defStr = religiousCombatStrength(defender->typeId());

    if (atkStr <= 0 || defStr <= 0) {
        return ErrorCode::InvalidArgument;
    }

    const UnitTypeDef& atkDef = unitTypeDef(attacker->typeId());
    const UnitTypeDef& defDef = unitTypeDef(defender->typeId());

    // Health modifier
    const float atkHP = static_cast<float>(attacker->hitPoints())
                      / static_cast<float>(atkDef.maxHitPoints);
    const float defHP = static_cast<float>(defender->hitPoints())
                      / static_cast<float>(defDef.maxHitPoints);

    const float atkPower = static_cast<float>(atkStr) * atkHP;
    const float defPower = static_cast<float>(defStr) * defHP;

    // Damage calculation (simplified Lanchester)
    const float ratio   = atkPower / std::max(0.01f, defPower);
    const int32_t atkDamage = static_cast<int32_t>(30.0f / std::max(0.5f, ratio));
    const int32_t defDamage = static_cast<int32_t>(30.0f * std::min(2.0f, ratio));

    attacker->setHitPoints(attacker->hitPoints() - atkDamage);
    defender->setHitPoints(defender->hitPoints() - defDamage);

    LOG_INFO("Theological combat: %.*s (%d HP) vs %.*s (%d HP)",
             static_cast<int>(atkDef.name.size()), atkDef.name.data(),
             attacker->hitPoints(),
             static_cast<int>(defDef.name.size()), defDef.name.data(),
             defender->hitPoints());

    // Remove dead units via their owning player
    if (defender->isDead()) {
        aoc::game::Player* defOwner = gameState.player(defender->owner());
        if (defOwner != nullptr) {
            defOwner->removeUnit(defender);
        }
    }
    if (attacker->isDead()) {
        aoc::game::Player* atkOwner = gameState.player(attacker->owner());
        if (atkOwner != nullptr) {
            atkOwner->removeUnit(attacker);
        }
    }

    return ErrorCode::Ok;
}

ErrorCode purgeReligion(aoc::game::GameState& gameState,
                         EntityId inquisitorEntity,
                         EntityId cityEntity) {
    aoc::game::Unit* inquisitor = findUnitByEntity(gameState, inquisitorEntity);
    if (inquisitor == nullptr || !inquisitor->hasCharges()) {
        return ErrorCode::InvalidArgument;
    }

    // The city is identified by its sequential index among all cities across
    // all players — same convention as units.
    uint32_t remaining = cityEntity.index;
    aoc::game::City* targetCity = nullptr;
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        const std::vector<std::unique_ptr<aoc::game::City>>& cities = playerPtr->cities();
        const uint32_t count = static_cast<uint32_t>(cities.size());
        if (remaining < count) {
            targetCity = cities[static_cast<std::size_t>(remaining)].get();
            break;
        }
        remaining -= count;
    }
    if (targetCity == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    // Derive the inquisitor's religion from its owner's founded religion
    const aoc::game::Player* inqOwner = gameState.player(inquisitor->owner());
    const uint8_t inquisitorReligion = (inqOwner != nullptr)
        ? static_cast<uint8_t>(inqOwner->faith().foundedReligion)
        : static_cast<uint8_t>(NO_RELIGION);

    // Remove all foreign religious pressure (keep only the inquisitor's religion)
    CityReligionComponent& cityRel = targetCity->religion();
    for (uint8_t r = 0; r < MAX_RELIGIONS; ++r) {
        if (r != inquisitorReligion) {
            cityRel.pressure[r] = 0.0f;
        }
    }

    inquisitor->useCharge();
    if (!inquisitor->hasCharges()) {
        aoc::game::Player* inqPlayer = gameState.player(inquisitor->owner());
        if (inqPlayer != nullptr) {
            inqPlayer->removeUnit(inquisitor);
        }
    }

    LOG_INFO("Inquisitor purged foreign religions from city");

    return ErrorCode::Ok;
}

} // namespace aoc::sim
