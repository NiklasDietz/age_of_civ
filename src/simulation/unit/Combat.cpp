/**
 * @file Combat.cpp
 * @brief Lanchester-based combat resolution.
 */

#include "aoc/simulation/unit/Combat.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/unit/CombatExtensions.hpp"
#include "aoc/simulation/diplomacy/WarWeariness.hpp"
#include "aoc/simulation/economy/DomesticCourier.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/map/RiverGameplay.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim {

float terrainDefenseModifier(const aoc::map::HexGrid& grid, aoc::hex::AxialCoord position) {
    if (!grid.isValid(position)) {
        return 1.0f;
    }
    int32_t index = grid.toIndex(position);
    aoc::map::FeatureType feature = grid.feature(index);

    float modifier = 1.0f;
    if (feature == aoc::map::FeatureType::Hills) {
        modifier += 0.3f;    // +30% defense on hills
    }
    if (feature == aoc::map::FeatureType::Forest) {
        modifier += 0.25f;   // +25% in forest
    }
    if (feature == aoc::map::FeatureType::Jungle) {
        modifier += 0.25f;
    }
    return modifier;
}

int32_t countAdjacentFriendlies(const aoc::game::GameState& gameState,
                                 aoc::hex::AxialCoord position,
                                 PlayerId friendlyPlayer) {
    std::array<aoc::hex::AxialCoord, 6> nbrs = aoc::hex::neighbors(position);
    int32_t count = 0;

    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        if (player->id() != friendlyPlayer) {
            continue;
        }
        for (const std::unique_ptr<aoc::game::Unit>& unit : player->units()) {
            for (const aoc::hex::AxialCoord& nbr : nbrs) {
                if (unit->position() == nbr) {
                    ++count;
                    break;
                }
            }
        }
    }
    return count;
}

float classMatchupModifier(UnitClass attackerClass, UnitClass defenderClass) {
    // AntiCavalry (spears/pikes/AT guns) vs Cavalry/Armor: +50%
    if (attackerClass == UnitClass::AntiCavalry
        && (defenderClass == UnitClass::Cavalry || defenderClass == UnitClass::Armor)) {
        return 1.50f;
    }
    // Cavalry vs Ranged/Artillery: +33% (fast flankers overwhelm slow shooters)
    if (attackerClass == UnitClass::Cavalry
        && (defenderClass == UnitClass::Ranged || defenderClass == UnitClass::Artillery)) {
        return 1.33f;
    }
    // Ranged vs Melee: +25% (kiting advantage)
    if (attackerClass == UnitClass::Ranged && defenderClass == UnitClass::Melee) {
        return 1.25f;
    }
    // Armor vs Melee/AntiCavalry: +25% (mechanised advantage over infantry)
    if (attackerClass == UnitClass::Armor
        && (defenderClass == UnitClass::Melee || defenderClass == UnitClass::AntiCavalry)) {
        return 1.25f;
    }
    // Artillery vs Armor: +33% (indirect fire vs slow heavy targets)
    if (attackerClass == UnitClass::Artillery && defenderClass == UnitClass::Armor) {
        return 1.33f;
    }
    // Air vs ground (excluding AntiCavalry which doubles as AA): +25%
    if (attackerClass == UnitClass::Air && defenderClass != UnitClass::AntiCavalry
        && defenderClass != UnitClass::Air && defenderClass != UnitClass::Helicopter) {
        return 1.25f;
    }
    // AntiCavalry vs Air/Helicopter: +50% (AA role in modern era)
    if (attackerClass == UnitClass::AntiCavalry
        && (defenderClass == UnitClass::Air || defenderClass == UnitClass::Helicopter)) {
        return 1.50f;
    }
    // Helicopter vs Artillery: +33% (gunships hunt artillery)
    if (attackerClass == UnitClass::Helicopter && defenderClass == UnitClass::Artillery) {
        return 1.33f;
    }
    return 1.0f;
}

namespace {

/// Core damage formula: modified Lanchester-style.
/// damage = 30 * (attackStrength / defenseStrength) * randomFactor.
/// Both operands are symmetrically floored to 0.01 to avoid insta-kill exploits
/// when a unit's effective strength collapses (embark + damaged + zero-terrain).
/// Upper bound prevents exotic stacking from overflowing the ratio.
int32_t computeDamage(float attackStrength, float defenseStrength, aoc::Random& rng) {
    const float atk = std::clamp(attackStrength, 0.01f, 1000.0f);
    const float def = std::clamp(defenseStrength, 0.01f, 1000.0f);

    const float ratio = atk / def;
    const float randomFactor = 0.8f + rng.nextFloat() * 0.4f;
    const float baseDamage = 30.0f * ratio * randomFactor;

    return std::clamp(static_cast<int32_t>(baseDamage), 0, 100);
}

/**
 * @brief Find and return the Player that owns the given unit pointer.
 *
 * Searches all players by pointer identity. Returns nullptr if not found,
 * which indicates a programming error (unit not registered with any player).
 */
aoc::game::Player* findOwningPlayer(aoc::game::GameState& gameState,
                                    const aoc::game::Unit* unit) {
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        for (const std::unique_ptr<aoc::game::Unit>& u : player->units()) {
            if (u.get() == unit) {
                return player.get();
            }
        }
    }
    return nullptr;
}

} // anonymous namespace

CombatResult resolveMeleeCombat(aoc::game::GameState& gameState,
                                 aoc::Random& rng,
                                 const aoc::map::HexGrid& grid,
                                 aoc::game::Unit& attacker,
                                 aoc::game::Unit& defender) {
    // Embarked defenders: melee attacks are rejected outright. Embarkation
    // cuts strength 50% and stacks with health + terrain penalties, letting
    // the defense strength collapse toward the insta-kill shortcut. Embarked
    // units remain engageable by ranged attacks (naval / archer).
    if (defender.state() == aoc::sim::UnitState::Embarked) {
        return CombatResult{0, 0, false, false, 0, 0};
    }

    const aoc::sim::UnitTypeDef& atkDef = attacker.typeDef();
    const aoc::sim::UnitTypeDef& defDef = defender.typeDef();

    // Effective strengths including formation bonus
    float atkStrength = static_cast<float>(atkDef.combatStrength);
    float defStrength = static_cast<float>(defDef.combatStrength);

    // WP-P2: starvation derate. -10% per consecutive starving turn, floor 50%.
    auto starveMult = [](int32_t turns) -> float {
        if (turns <= 0) { return 1.0f; }
        const float mult = 1.0f - 0.1f * static_cast<float>(turns);
        return std::max(0.5f, mult);
    };
    atkStrength *= starveMult(attacker.turnsStarving());
    defStrength *= starveMult(defender.turnsStarving());

    atkStrength *= formationStrengthMultiplier(attacker.formationLevel());
    defStrength *= formationStrengthMultiplier(defender.formationLevel());

    // Promotion combat bonuses (Battlecry, Tortoise, Elite, etc.).
    atkStrength += static_cast<float>(attacker.experience().totalCombatBonus());
    defStrength += static_cast<float>(defender.experience().totalCombatBonus());

    // Civ-specific unique unit bonuses. Applies only when the unit's type
    // matches the civ's `uniqueUnit.baseUnit` — Civ-6 style: Romans get the
    // Legion bonus only on Swordsmen, not on Warriors.
    {
        const aoc::game::Player* atkP = gameState.player(attacker.owner());
        const aoc::game::Player* defP = gameState.player(defender.owner());
        if (atkP != nullptr) {
            const aoc::sim::CivilizationDef& cd = aoc::sim::civDef(atkP->civId());
            if (cd.uniqueUnit.baseUnit == attacker.typeId()) {
                atkStrength += static_cast<float>(cd.uniqueUnit.combatBonus);
            }
        }
        if (defP != nullptr) {
            const aoc::sim::CivilizationDef& cd = aoc::sim::civDef(defP->civId());
            if (cd.uniqueUnit.baseUnit == defender.typeId()) {
                defStrength += static_cast<float>(cd.uniqueUnit.combatBonus);
            }
        }
    }

    // Civ ability: flat combat strength bonus for land units.
    {
        const aoc::game::Player* atkPlayer = gameState.player(attacker.owner());
        const aoc::game::Player* defPlayer = gameState.player(defender.owner());
        if (atkPlayer != nullptr) {
            const aoc::sim::CivAbilityModifiers& m =
                aoc::sim::civDef(atkPlayer->civId()).modifiers;
            atkStrength += m.combatStrengthBonus;
            // Conditional combat bonuses keyed off attacker tile / context.
            const int32_t atkIdx = grid.toIndex(attacker.position());
            if (m.combatBonusOwnTerritory > 0
             && grid.owner(atkIdx) == attacker.owner()) {
                atkStrength += static_cast<float>(m.combatBonusOwnTerritory);
            }
            if (m.combatBonusInForest > 0
             && grid.feature(atkIdx) == aoc::map::FeatureType::Forest) {
                atkStrength += static_cast<float>(m.combatBonusInForest);
            }
            if (m.combatBonusVsDifferentReligion > 0 && defPlayer != nullptr) {
                const ReligionId atkR = atkPlayer->faith().foundedReligion;
                const ReligionId defR = defPlayer->faith().foundedReligion;
                if (atkR != aoc::sim::NO_RELIGION
                 && defR != aoc::sim::NO_RELIGION
                 && atkR != defR) {
                    atkStrength += static_cast<float>(m.combatBonusVsDifferentReligion);
                }
            }
        }
        if (defPlayer != nullptr) {
            const aoc::sim::CivAbilityModifiers& m =
                aoc::sim::civDef(defPlayer->civId()).modifiers;
            defStrength += m.combatStrengthBonus;
            const int32_t defIdx = grid.toIndex(defender.position());
            if (m.combatBonusOwnTerritory > 0
             && grid.owner(defIdx) == defender.owner()) {
                defStrength += static_cast<float>(m.combatBonusOwnTerritory);
            }
            if (m.combatBonusInForest > 0
             && grid.feature(defIdx) == aoc::map::FeatureType::Forest) {
                defStrength += static_cast<float>(m.combatBonusInForest);
            }
        }
    }

    // Embarked units fight at 50% strength
    if (attacker.state() == aoc::sim::UnitState::Embarked) {
        atkStrength *= 0.5f;
    }
    if (defender.state() == aoc::sim::UnitState::Embarked) {
        defStrength *= 0.5f;
    }

    // Health modifier: damaged units fight worse
    float atkHealthMod = static_cast<float>(attacker.hitPoints()) / static_cast<float>(atkDef.maxHitPoints);
    float defHealthMod = static_cast<float>(defender.hitPoints()) / static_cast<float>(defDef.maxHitPoints);
    atkStrength *= atkHealthMod;
    defStrength *= defHealthMod;

    // Terrain defense bonus for defender
    float terrainMod = terrainDefenseModifier(grid, defender.position());
    defStrength *= terrainMod;

    // River crossing penalty: defender gets +25% if attacker must cross a river
    {
        int32_t atkIdx = grid.toIndex(attacker.position());
        int32_t defIdx = grid.toIndex(defender.position());
        if (aoc::map::crossesRiver(grid, atkIdx, defIdx)) {
            defStrength *= aoc::map::RIVER_DEFENSE_BONUS;
        }
        // Elevation advantage: +10% per level difference
        float elevMod = aoc::map::elevationCombatModifier(grid, atkIdx, defIdx);
        atkStrength *= elevMod;
    }

    // Flanking bonus: +10% per adjacent friendly for attacker, capped at 3
    // adjacent (max +30%). Without the cap, a 3-unit Army surrounding a
    // single defender produces 1.6x strength plus reduced counter-damage,
    // snowballing into near-zero-loss kills every turn.
    int32_t flanking = std::min(3,
        countAdjacentFriendlies(gameState, defender.position(), attacker.owner()));
    atkStrength *= 1.0f + static_cast<float>(flanking) * 0.10f;

    // Class matchup bonus (rock-paper-scissors)
    atkStrength *= classMatchupModifier(atkDef.unitClass, defDef.unitClass);
    defStrength *= classMatchupModifier(defDef.unitClass, atkDef.unitClass);

    // Fortification bonus
    if (defender.state() == aoc::sim::UnitState::Fortified) {
        defStrength *= 1.25f;
    }

    // War weariness combat penalty: iterate players to find attacker and defender owners
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        const aoc::sim::PlayerWarWearinessComponent& ww = player->warWeariness();
        if (player->id() == attacker.owner()) {
            atkStrength *= warWearinessCombatModifier(ww.weariness);
        }
        if (player->id() == defender.owner()) {
            defStrength *= warWearinessCombatModifier(ww.weariness);
        }
    }

    // Calculate damage
    CombatResult result{};
    result.defenderDamage = computeDamage(atkStrength, defStrength, rng);
    result.attackerDamage = computeDamage(defStrength, atkStrength, rng);

    // Counter-damage discount applies symmetrically: both sides take 80% of
    // the rolled damage. Previously only the attacker received this, which
    // combined with flanking (caught a 1.6x strength + 0.8x damage-taken)
    // to produce near-zero-loss kills. Keeping the reduction mutual tunes
    // combat to a less lethal tempo without favoring the aggressor.
    result.attackerDamage = result.attackerDamage * 8 / 10;
    result.defenderDamage = result.defenderDamage * 8 / 10;

    // Apply damage
    attacker.setHitPoints(attacker.hitPoints() - result.attackerDamage);
    defender.setHitPoints(defender.hitPoints() - result.defenderDamage);

    result.defenderKilled = defender.isDead();
    result.attackerKilled = attacker.isDead();

    // XP: base 5, bonus for killing.
    result.attackerXpGained = 5;
    result.defenderXpGained = 4;
    if (result.defenderKilled) {
        result.attackerXpGained += 10;
    }
    if (result.attackerKilled) {
        result.defenderXpGained += 10;
    }
    // Apply XP to survivors now — applying after the removeUnit() calls
    // below would either dangle (if the side died) or require re-lookup.
    // Dead units don't need XP; a killed side's `xpGained` still reports
    // for UI/analytics but never touches the unit.
    if (!result.attackerKilled) {
        attacker.experience().addExperience(result.attackerXpGained);
    }
    if (!result.defenderKilled) {
        defender.experience().addExperience(result.defenderXpGained);
    }

    // If defender died and attacker survived, move attacker to defender's tile
    aoc::hex::AxialCoord defenderTile = defender.position();
    if (result.defenderKilled && !result.attackerKilled) {
        attacker.setPosition(defenderTile);
        attacker.setMovementRemaining(0);  // Melee attack ends movement
    }

    // Snapshot info before units are removed (removeUnit frees memory).
    PlayerId attackerOwner = attacker.owner();
    PlayerId defenderOwner = defender.owner();
    const int32_t defenderProductionCost = defender.typeDef().productionCost;
    const std::string_view defenderName = defender.typeDef().name;

    // Snapshot Courier cargo if the defender is an undelivered domestic courier.
    // Cargo is lost; attacker loots half basePrice * quantity as plunder gold.
    const bool defenderIsCourier = (defender.typeId().value == 32);
    const uint16_t defenderCargoGoodId   = defenderIsCourier ? defender.courier().goodId   : uint16_t{0};
    const int32_t  defenderCargoQuantity = (defenderIsCourier && !defender.courier().delivered)
                                            ? defender.courier().quantity : 0;

    // Clean up dead units: find the owning player and remove the unit
    if (result.defenderKilled) {
        aoc::game::Player* defPlayer = findOwningPlayer(gameState, &defender);
        if (defPlayer != nullptr) {
            // Stack kill: if the defender died on open terrain (no city, no fort),
            // all other units of the same owner on that tile are also destroyed.
            bool tileHasCity = false;
            for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
                if (player->cityAt(defenderTile) != nullptr) {
                    tileHasCity = true;
                    break;
                }
            }
            bool tileHasFort = (grid.improvement(grid.toIndex(defenderTile)) == aoc::map::ImprovementType::Fort);

            if (!tileHasCity && !tileHasFort) {
                // Collect pointers to stack units before any removal to avoid
                // invalidating the vector while iterating. Units in a Corps or
                // Army formation are excluded — a formation represents a
                // single logical force whose HP is what the defender already
                // rolled, so removing its component units on an open-terrain
                // stack-kill would double-punish the defender.
                std::vector<aoc::game::Unit*> stackKill;
                for (const std::unique_ptr<aoc::game::Unit>& u : defPlayer->units()) {
                    if (u.get() == &defender) { continue; }
                    if (u->position() != defenderTile) { continue; }
                    if (u->formationLevel() != aoc::sim::FormationLevel::Single) { continue; }
                    stackKill.push_back(u.get());
                }
                for (aoc::game::Unit* killed : stackKill) {
                    defPlayer->removeUnit(killed);
                }
                if (!stackKill.empty()) {
                    LOG_INFO("Stack kill: %d units destroyed on open terrain at (%d,%d)",
                             static_cast<int>(stackKill.size()),
                             defenderTile.q, defenderTile.r);
                }
            }

            defPlayer->removeUnit(&defender);
            // defender reference is now DANGLING — do not use after this point
        }
    }
    if (result.attackerKilled) {
        aoc::game::Player* atkPlayer = findOwningPlayer(gameState, &attacker);
        if (atkPlayer != nullptr) {
            atkPlayer->removeUnit(&attacker);
            // attacker reference is now DANGLING — do not use after this point
        }
    }

    // Military economic benefits when a unit is killed.
    // Uses snapshotted values (defenderProductionCost, defenderName) because
    // the defender Unit has already been freed by removeUnit above.
    if (result.defenderKilled && !result.attackerKilled) {
        aoc::game::Player* atkPlayer = gameState.player(attackerOwner);

        if (defenderOwner == BARBARIAN_PLAYER && atkPlayer != nullptr) {
            // Barbarian encampment clearance bonus
            atkPlayer->addGold(25);
            LOG_INFO("Player %u earned 25 gold from clearing barbarian encampment",
                     static_cast<unsigned>(attackerOwner));
        } else if (atkPlayer != nullptr) {
            // Pillaging: destroying an enemy unit yields resources.
            // Gold from the unit's production cost (30% of cost as plunder).
            const CurrencyAmount plunderGold =
                static_cast<CurrencyAmount>(defenderProductionCost * 3 / 10);
            if (plunderGold > 0) {
                atkPlayer->addGold(plunderGold);
                LOG_INFO("Player %u pillaged %lld gold from destroying %.*s",
                         static_cast<unsigned>(attackerOwner),
                         static_cast<long long>(plunderGold),
                         static_cast<int>(defenderName.size()),
                         defenderName.data());
            }

            // Courier cargo loot: half basePrice * quantity of lost cargo.
            if (defenderIsCourier && defenderCargoQuantity > 0) {
                const aoc::sim::GoodDef& gd = aoc::sim::goodDef(defenderCargoGoodId);
                const CurrencyAmount cargoGold =
                    static_cast<CurrencyAmount>((gd.basePrice * defenderCargoQuantity) / 2);
                if (cargoGold > 0) {
                    atkPlayer->addGold(cargoGold);
                    LOG_INFO("Player %u looted courier cargo: good %u x%d for %lld gold",
                             static_cast<unsigned>(attackerOwner),
                             static_cast<unsigned>(defenderCargoGoodId),
                             defenderCargoQuantity,
                             static_cast<long long>(cargoGold));
                }
            }

            // Bonus pillage gold if the tile has improvements or resources.
            // Represents looting infrastructure — like Civ 6's pillaging.
            const int32_t tileIdx = grid.toIndex(defenderTile);
            if (grid.resource(tileIdx).isValid()
                || grid.improvement(tileIdx) != aoc::map::ImprovementType::None) {
                constexpr CurrencyAmount TILE_PILLAGE_BONUS = 15;
                atkPlayer->addGold(TILE_PILLAGE_BONUS);
                LOG_INFO("Player %u pillaged tile improvements at (%d,%d) for %lld gold",
                         static_cast<unsigned>(attackerOwner),
                         defenderTile.q, defenderTile.r,
                         static_cast<long long>(TILE_PILLAGE_BONUS));
            }

            // Conditional civ bonus: scienceOnUnitKill (Macedon),
            // faithOnUnitKill (Aztec). Bypassed against Barbarians above.
            const aoc::sim::CivAbilityModifiers& m =
                aoc::sim::civDef(atkPlayer->civId()).modifiers;
            if (m.scienceOnUnitKill > 0) {
                atkPlayer->tech().researchProgress +=
                    static_cast<float>(m.scienceOnUnitKill);
            }
            if (m.faithOnUnitKill > 0) {
                atkPlayer->faith().faith +=
                    static_cast<float>(m.faithOnUnitKill);
            }
        }
    }

    LOG_INFO("Atk took %d dmg, Def took %d dmg%s%s",
             result.attackerDamage, result.defenderDamage,
             result.attackerKilled ? " (attacker killed)" : "",
             result.defenderKilled ? " (defender killed)" : "");

    return result;
}

CombatResult resolveRangedCombat(aoc::game::GameState& gameState,
                                  aoc::Random& rng,
                                  const aoc::map::HexGrid& grid,
                                  aoc::game::Unit& attacker,
                                  aoc::game::Unit& defender) {
    const aoc::sim::UnitTypeDef& atkDef = attacker.typeDef();
    const aoc::sim::UnitTypeDef& defDef = defender.typeDef();

    // Ranged uses rangedStrength for attack, combatStrength for defense
    float atkStrength = static_cast<float>(atkDef.rangedStrength);
    float defStrength = static_cast<float>(defDef.combatStrength);

    // WP-P2: starvation derate.
    auto rngStarveMult = [](int32_t turns) -> float {
        if (turns <= 0) { return 1.0f; }
        return std::max(0.5f, 1.0f - 0.1f * static_cast<float>(turns));
    };
    atkStrength *= rngStarveMult(attacker.turnsStarving());
    defStrength *= rngStarveMult(defender.turnsStarving());

    // Civ unique-unit bonuses (ranged uses rangedBonus for atk, combatBonus for def).
    {
        const aoc::game::Player* atkP = gameState.player(attacker.owner());
        const aoc::game::Player* defP = gameState.player(defender.owner());
        if (atkP != nullptr) {
            const aoc::sim::CivilizationDef& cd = aoc::sim::civDef(atkP->civId());
            if (cd.uniqueUnit.baseUnit == attacker.typeId()) {
                atkStrength += static_cast<float>(cd.uniqueUnit.rangedBonus);
            }
        }
        if (defP != nullptr) {
            const aoc::sim::CivilizationDef& cd = aoc::sim::civDef(defP->civId());
            if (cd.uniqueUnit.baseUnit == defender.typeId()) {
                defStrength += static_cast<float>(cd.uniqueUnit.combatBonus);
            }
        }
    }

    // Formation multiplier (symmetric with melee).
    atkStrength *= formationStrengthMultiplier(attacker.formationLevel());
    defStrength *= formationStrengthMultiplier(defender.formationLevel());

    // Promotion combat bonuses (symmetric with melee).
    atkStrength += static_cast<float>(attacker.experience().totalCombatBonus());
    defStrength += static_cast<float>(defender.experience().totalCombatBonus());

    float atkHealthMod = static_cast<float>(attacker.hitPoints()) / static_cast<float>(atkDef.maxHitPoints);
    float defHealthMod = static_cast<float>(defender.hitPoints()) / static_cast<float>(defDef.maxHitPoints);
    atkStrength *= atkHealthMod;
    defStrength *= defHealthMod;

    float terrainMod = terrainDefenseModifier(grid, defender.position());
    defStrength *= terrainMod;

    // River crossing penalty and elevation advantage
    {
        int32_t atkIdx = grid.toIndex(attacker.position());
        int32_t defIdx = grid.toIndex(defender.position());
        if (aoc::map::crossesRiver(grid, atkIdx, defIdx)) {
            defStrength *= aoc::map::RIVER_DEFENSE_BONUS;
        }
        float elevMod = aoc::map::elevationCombatModifier(grid, atkIdx, defIdx);
        atkStrength *= elevMod;
    }

    // Flanking bonus: matches melee rules (capped at 3 adjacent).
    int32_t flanking = std::min(3,
        countAdjacentFriendlies(gameState, defender.position(), attacker.owner()));
    atkStrength *= 1.0f + static_cast<float>(flanking) * 0.10f;

    // Class matchup bonus applied symmetrically to both sides.
    atkStrength *= classMatchupModifier(atkDef.unitClass, defDef.unitClass);
    defStrength *= classMatchupModifier(defDef.unitClass, atkDef.unitClass);

    // Fortification bonus for defender (same as melee).
    if (defender.state() == aoc::sim::UnitState::Fortified) {
        defStrength *= 1.25f;
    }

    // War weariness combat penalty (ranged)
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        const aoc::sim::PlayerWarWearinessComponent& ww = player->warWeariness();
        if (player->id() == attacker.owner()) {
            atkStrength *= warWearinessCombatModifier(ww.weariness);
        }
        if (player->id() == defender.owner()) {
            defStrength *= warWearinessCombatModifier(ww.weariness);
        }
    }

    CombatResult result{};
    // Ranged: attacker deals damage but takes none (no retaliation)
    result.defenderDamage = computeDamage(atkStrength, defStrength, rng);
    result.attackerDamage = 0;

    defender.setHitPoints(defender.hitPoints() - result.defenderDamage);
    result.defenderKilled = defender.isDead();

    result.attackerXpGained = 3;
    result.defenderXpGained = 2;
    if (result.defenderKilled) {
        result.attackerXpGained += 8;
    }
    // Apply XP to survivors before any removal. Ranged attacker can't die
    // from counter-fire (attackerDamage = 0 above) so always gets XP;
    // defender only gets XP if still alive.
    attacker.experience().addExperience(result.attackerXpGained);
    if (!result.defenderKilled) {
        defender.experience().addExperience(result.defenderXpGained);
    } else {
        aoc::game::Player* defPlayer = findOwningPlayer(gameState, &defender);
        if (defPlayer != nullptr) {
            defPlayer->removeUnit(&defender);
        }
    }

    return result;
}

CombatPreview previewCombat(const aoc::game::GameState& gameState,
                             const aoc::map::HexGrid& grid,
                             const aoc::game::Unit& attacker,
                             const aoc::game::Unit& defender) {
    const aoc::sim::UnitTypeDef& atkDef = attacker.typeDef();
    const aoc::sim::UnitTypeDef& defDef = defender.typeDef();

    // Determine if ranged or melee
    const bool isRanged = (atkDef.rangedStrength > 0);

    // Effective strengths
    float atkStrength = isRanged
        ? static_cast<float>(atkDef.rangedStrength)
        : static_cast<float>(atkDef.combatStrength);
    float defStrength = static_cast<float>(defDef.combatStrength);

    // WP-P2: starvation derate (preview must match resolution).
    auto previewStarveMult = [](int32_t turns) -> float {
        if (turns <= 0) { return 1.0f; }
        return std::max(0.5f, 1.0f - 0.1f * static_cast<float>(turns));
    };
    atkStrength *= previewStarveMult(attacker.turnsStarving());
    defStrength *= previewStarveMult(defender.turnsStarving());

    // Embarked modifier
    if (attacker.state() == aoc::sim::UnitState::Embarked) {
        atkStrength *= 0.5f;
    }
    if (defender.state() == aoc::sim::UnitState::Embarked) {
        defStrength *= 0.5f;
    }

    // Health modifier
    float atkHealthMod = static_cast<float>(attacker.hitPoints()) / static_cast<float>(atkDef.maxHitPoints);
    float defHealthMod = static_cast<float>(defender.hitPoints()) / static_cast<float>(defDef.maxHitPoints);
    atkStrength *= atkHealthMod;
    defStrength *= defHealthMod;

    // Terrain defense bonus for defender
    float terrainMod = terrainDefenseModifier(grid, defender.position());
    defStrength *= terrainMod;

    // River crossing penalty and elevation advantage
    {
        int32_t atkIdx = grid.toIndex(attacker.position());
        int32_t defIdx = grid.toIndex(defender.position());
        if (aoc::map::crossesRiver(grid, atkIdx, defIdx)) {
            defStrength *= aoc::map::RIVER_DEFENSE_BONUS;
        }
        float elevMod = aoc::map::elevationCombatModifier(grid, atkIdx, defIdx);
        atkStrength *= elevMod;
    }

    // Flanking bonus for melee
    if (!isRanged) {
        int32_t flanking = countAdjacentFriendlies(gameState, defender.position(), attacker.owner());
        atkStrength *= 1.0f + static_cast<float>(flanking) * 0.10f;
    }

    // Class matchup bonus
    atkStrength *= classMatchupModifier(atkDef.unitClass, defDef.unitClass);
    if (!isRanged) {
        defStrength *= classMatchupModifier(defDef.unitClass, atkDef.unitClass);
    }

    // Fortification bonus
    if (defender.state() == aoc::sim::UnitState::Fortified) {
        defStrength *= 1.25f;
    }

    // Compute damage with randomFactor = 1.0 (average outcome)
    CombatPreview preview{};

    if (defStrength < 0.01f) {
        preview.expectedDefenderDamage = 100;
    } else {
        float ratio = atkStrength / defStrength;
        float baseDamage = 30.0f * ratio * 1.0f;
        preview.expectedDefenderDamage = std::clamp(static_cast<int32_t>(baseDamage), 0, 100);
    }

    if (isRanged) {
        preview.expectedAttackerDamage = 0;
    } else {
        if (atkStrength < 0.01f) {
            preview.expectedAttackerDamage = 100;
        } else {
            float counterRatio = defStrength / atkStrength;
            float counterDamage = 30.0f * counterRatio * 1.0f;
            int32_t rawCounter = std::clamp(static_cast<int32_t>(counterDamage), 0, 100);
            // Attacker takes 80% of counter-damage (aggressor advantage)
            preview.expectedAttackerDamage = rawCounter * 8 / 10;
        }
    }

    return preview;
}

// ============================================================================
// Legacy EntityId overloads
// ============================================================================

/**
 * @brief Resolve an EntityId to a Unit via index across all players' unit vectors.
 *
 * The n-th unit overall (counting across all players in order) maps to EntityId{n}.
 * This bridges the legacy EntityId callers to the GameState object model without
 * touching the ECS World.
 */
static aoc::game::Unit* findUnitByEntity(aoc::game::GameState& gameState, EntityId entity) {
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

CombatResult resolveMeleeCombat(aoc::game::GameState& gameState,
                                 aoc::Random& rng,
                                 const aoc::map::HexGrid& grid,
                                 EntityId attackerEntity,
                                 EntityId defenderEntity) {
    aoc::game::Unit* attacker = findUnitByEntity(gameState, attackerEntity);
    aoc::game::Unit* defender = findUnitByEntity(gameState, defenderEntity);
    if (attacker == nullptr || defender == nullptr) { return {}; }
    return resolveMeleeCombat(gameState, rng, grid, *attacker, *defender);
}

CombatResult resolveRangedCombat(aoc::game::GameState& gameState,
                                  aoc::Random& rng,
                                  const aoc::map::HexGrid& grid,
                                  EntityId attackerEntity,
                                  EntityId defenderEntity) {
    aoc::game::Unit* attacker = findUnitByEntity(gameState, attackerEntity);
    aoc::game::Unit* defender = findUnitByEntity(gameState, defenderEntity);
    if (attacker == nullptr || defender == nullptr) { return {}; }
    return resolveRangedCombat(gameState, rng, grid, *attacker, *defender);
}

static const aoc::game::Unit* findConstUnitByEntity(const aoc::game::GameState& gameState,
                                                     EntityId entity) {
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

CombatPreview previewCombat(const aoc::game::GameState& gameState,
                             const aoc::map::HexGrid& grid,
                             EntityId attackerEntity,
                             EntityId defenderEntity) {
    const aoc::game::Unit* attacker = findConstUnitByEntity(gameState, attackerEntity);
    const aoc::game::Unit* defender = findConstUnitByEntity(gameState, defenderEntity);
    if (attacker == nullptr || defender == nullptr) { return {}; }
    return previewCombat(gameState, grid, *attacker, *defender);
}

} // namespace aoc::sim
