/**
 * @file CitySiege.cpp
 * @brief City hit points, the bombard-then-capture loop, and the capture
 *        bookkeeping both the melee attack and the walk-in share.
 */

#include "aoc/simulation/city/CitySiege.hpp"

#include "aoc/core/Log.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/simulation/city/CityBombardment.hpp"
#include "aoc/simulation/citystate/CityState.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/tech/EraProgression.hpp"
#include "aoc/simulation/unit/Combat.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"

#include <algorithm>
#include <memory>

namespace aoc::sim {

namespace {

/// Melee reaching a walled city chips the wall rather than the city, the same
/// 15% share CityBombardment.hpp documents for a unit pressing into walls.
constexpr float MELEE_WALL_SHARE = 0.15f;

/// Siege-class units are what walls are afraid of; everyone else's fire is
/// half-absorbed by the masonry.
[[nodiscard]] float wallDamageShare(UnitClass unitClass) {
    return (unitClass == UnitClass::Artillery) ? 1.0f : 0.5f;
}

/// Strength the best military unit standing in the city adds to its defence.
[[nodiscard]] int32_t garrisonStrength(const aoc::game::GameState& gameState,
                                       const aoc::game::City& city) {
    const aoc::game::Player* owner = gameState.player(city.owner());
    if (owner == nullptr) {
        return 0;
    }
    int32_t best = 0;
    for (const std::unique_ptr<aoc::game::Unit>& unit : owner->units()) {
        if (unit == nullptr || unit->position() != city.location() || !unit->isMilitary()) {
            continue;
        }
        best = std::max(best, static_cast<int32_t>(unit->typeDef().combatStrength));
    }
    return best;
}

} // namespace

int32_t cityDefenceStrength(const aoc::game::GameState& gameState, const aoc::game::City& city) {
    // A city is never free: an empty, wall-less village still fights back a
    // little, and each era of its owner's research hardens it.
    constexpr int32_t BASE_DEFENCE = 12;
    constexpr int32_t PER_ERA      = 6;

    int32_t strength               = BASE_DEFENCE;
    const aoc::game::Player* owner = gameState.player(city.owner());
    if (owner != nullptr) {
        strength += PER_ERA * static_cast<int32_t>(effectiveEraFromTech(*owner).value);
    }
    strength += city.walls().rangedStrength / 2;
    strength += garrisonStrength(gameState, city);
    return strength;
}

aoc::game::City* enemyCityAt(aoc::game::GameState& gameState, PlayerId viewer, hex::AxialCoord at) {
    aoc::game::Player* holder = gameState.cityHolder(at);
    if (holder == nullptr) {
        return nullptr;
    }
    aoc::game::City* city = holder->cityAt(at);
    if (city == nullptr || city->owner() == viewer) {
        return nullptr;
    }
    return city;
}

CityAttackResult resolveAttackOnCity(aoc::game::GameState& gameState, aoc::Random& rng,
                                     aoc::map::HexGrid& grid, aoc::game::Unit& attacker,
                                     aoc::game::City& city, int32_t currentTurn) {
    CityAttackResult result{};
    const UnitTypeDef& def  = attacker.typeDef();
    const bool ranged       = def.rangedStrength > 0 && def.range > 0;
    const float attackPower = static_cast<float>(ranged ? def.rangedStrength : def.combatStrength) *
                              (static_cast<float>(attacker.hitPoints()) /
                               static_cast<float>(std::max(1, attacker.typeDef().maxHitPoints)));
    const float defencePower = static_cast<float>(cityDefenceStrength(gameState, city));

    const int32_t rolled           = computeCombatDamage(attackPower, defencePower, rng);
    city.combat().lastAttackedTurn = currentTurn;

    if (!ranged) {
        // Melee. Intact walls turn it back; the attacker still chips the
        // masonry and pays for the attempt.
        result.attackerDamage = computeCombatDamage(defencePower, attackPower, rng);
        if (city.walls().isIntact()) {
            result.wallDamage = city.walls().takeDamage(
                std::max(1, static_cast<int32_t>(static_cast<float>(rolled) * MELEE_WALL_SHARE)));
            result.repelled = true;
        } else if (city.combat().isAlive()) {
            result.cityDamage = std::min(rolled, city.combat().hp);
            city.combat().hp -= result.cityDamage;
        }
        attacker.setHitPoints(attacker.hitPoints() - result.attackerDamage);
        if (attacker.hitPoints() <= 0) {
            attacker.setHitPoints(1); // A city never kills outright; it repels.
        }
        if (!city.walls().isIntact() && !city.combat().isAlive()) {
            captureCity(gameState, grid, attacker, city);
            result.captured = true;
        }
        return result;
    }

    // Ranged and bombard fire: walls first, then the city itself. Whatever the
    // masonry does not absorb carries through in the same shot.
    const float share = wallDamageShare(def.unitClass);
    int32_t remaining = rolled;
    if (city.walls().isIntact()) {
        const int32_t atWalls =
            std::max(1, static_cast<int32_t>(static_cast<float>(rolled) * share));
        result.wallDamage = city.walls().takeDamage(atWalls);
        remaining -= result.wallDamage;
    }
    if (remaining > 0 && city.combat().isAlive()) {
        result.cityDamage = std::min(remaining, city.combat().hp);
        city.combat().hp -= result.cityDamage;
    }
    return result;
}

CityAttackResult pressIntoCity(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                               aoc::game::Unit& attacker, aoc::game::City& city,
                               int32_t currentTurn) {
    CityAttackResult result{};
    city.combat().lastAttackedTurn = currentTurn;

    if (city.walls().isIntact()) {
        result.wallDamage = dealSiegeDamage(city, attacker);
        // Siege-specialist civs (Ottoman, Aztec) crack masonry faster.
        const aoc::game::Player* owner = gameState.player(attacker.owner());
        const int32_t bonus =
            (owner != nullptr) ? civDef(owner->civId()).modifiers.combatBonusVsCities : 0;
        if (bonus > 0) {
            result.wallDamage += city.walls().takeDamage(bonus);
        }
        result.repelled = true;
        return result;
    }

    // The walls are down, so the walk-in takes the city, exactly as it did
    // before cities had hit points.
    //
    // KNOWN GAP: this path ignores city hit points. Grinding them down here
    // was the obvious next step and was measured: it removes conquest from the
    // game outright -- seed 42, 4 players, 500 turns went from 43 captures to
    // 0, the run ending on a loyalty collapse instead -- because the AI has no
    // behaviour that parks next to a city and keeps shelling it. City hit
    // points therefore govern deliberate attacks (the human's right-click, the
    // debug route, the MCP tool and the AI shelling a city it already stands
    // beside), and the walk-in keeps the old rule until that AI behaviour
    // lands with the rest of Phase 3.2.
    captureCity(gameState, grid, attacker, city);
    result.captured = true;
    return result;
}

void captureCity(aoc::game::GameState& gameState, aoc::map::HexGrid& grid, aoc::game::Unit& captor,
                 aoc::game::City& city) {
    const PlayerId previousOwner   = city.owner();
    const hex::AxialCoord location = city.location();
    aoc::game::City* taken         = gameState.transferCity(location, captor.owner());
    if (taken == nullptr) {
        return;
    }
    if (taken->population() > 1) {
        taken->setPopulation(taken->population() - 1);
    }
    taken->production().queue.clear();

    // WP-D1: a captured city starts above the unrest threshold so it does not
    // flip straight back, and its walls are rebuilt to Medieval so retaking it
    // is another siege rather than a walk.
    taken->loyalty().loyalty     = 60.0f;
    taken->loyalty().unrestTurns = 0;
    taken->walls().setTier(WallTier::Medieval);
    taken->combat().hp               = taken->combat().maxHP;
    taken->combat().lastAttackedTurn = -1000;

    // WP-D3: a civ with no city left is out, which the Domination ratio needs.
    aoc::game::Player* previous = gameState.player(previousOwner);
    if (previous != nullptr && !previous->victoryTracker().isEliminated) {
        bool stillHasOwnedCity = false;
        for (const std::unique_ptr<aoc::game::Player>& holder : gameState.players()) {
            for (const std::unique_ptr<aoc::game::City>& c : holder->cities()) {
                if (c != nullptr && c->owner() == previousOwner) {
                    stillHasOwnedCity = true;
                    break;
                }
            }
            if (stillHasOwnedCity) {
                break;
            }
        }
        if (!stillHasOwnedCity) {
            previous->victoryTracker().isEliminated = true;
            LOG_INFO("Player %u eliminated (last city captured by Player %u)",
                     static_cast<unsigned>(previousOwner), static_cast<unsigned>(captor.owner()));
            aoc::game::Player* winner = gameState.player(captor.owner());
            if (winner != nullptr) {
                winner->victoryTracker().eraVictoryPoints += 100;
            }
        }
    }

    // Score VP per capture, plus the raiding civs' loot.
    aoc::game::Player* captorPlayer = gameState.player(captor.owner());
    if (captorPlayer != nullptr) {
        const int32_t vp =
            (taken->isOriginalCapital() && taken->originalOwner() != captor.owner()) ? 25 : 5;
        captorPlayer->victoryTracker().eraVictoryPoints += vp;
        const int32_t loot = civDef(captorPlayer->civId()).modifiers.goldOnCityCapture;
        if (loot > 0) {
            captorPlayer->monetary().treasury +=
                static_cast<int64_t>(loot) * std::max(1, taken->population());
        }
    }

    for (const hex::AxialCoord& workedTile : taken->workedTiles()) {
        if (grid.isValid(workedTile)) {
            const int32_t index = grid.toIndex(workedTile);
            if (grid.owner(index) == previousOwner) {
                grid.setOwner(index, captor.owner());
            }
        }
    }

    captor.setPosition(location);
    captor.setMovementRemaining(0);
    captor.clearPath();
    LOG_INFO("City %s captured by player %u (was player %u)", taken->name().c_str(),
             static_cast<unsigned>(captor.owner()), static_cast<unsigned>(previousOwner));
    // A fallen city leaves an antiquity site (v14 layer) for a later dig.
    grid.setAntiquitySite(grid.toIndex(location), 1);
}

void healCities(aoc::game::GameState& gameState, PlayerId player, int32_t currentTurn) {
    aoc::game::Player* owner = gameState.player(player);
    if (owner == nullptr) {
        return;
    }
    for (const std::unique_ptr<aoc::game::City>& city : owner->cities()) {
        if (city == nullptr || city->owner() != player) {
            continue;
        }
        CityCombatState& combat = city->combat();
        if (currentTurn - combat.lastAttackedTurn < CITY_HEAL_DELAY_TURNS) {
            continue;
        }
        combat.hp = std::min(combat.hp + CITY_HEAL_PER_TURN, combat.maxHP);
    }
}

} // namespace aoc::sim
