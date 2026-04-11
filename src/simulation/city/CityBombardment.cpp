/**
 * @file CityBombardment.cpp
 * @brief City ranged bombardment implementation.
 *
 * Migrated from ECS to GameState object model.
 */

#include "aoc/simulation/city/CityBombardment.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/core/Random.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim {

void processCityBombardment(aoc::game::GameState& gameState,
                             const aoc::map::HexGrid& grid,
                             PlayerId player, aoc::Random& rng) {
    aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) {
        return;
    }

    for (const std::unique_ptr<aoc::game::City>& city : gsPlayer->cities()) {
        // Check if the city has Walls (BuildingId 17)
        if (!city->hasBuilding(BuildingId{17})) {
            continue;
        }

        const float bombardStrength = 20.0f;

        // Find adjacent enemy military units across all players
        const std::array<aoc::hex::AxialCoord, 6> neighbors =
            aoc::hex::neighbors(city->location());

        for (const aoc::hex::AxialCoord& nbr : neighbors) {
            if (!grid.isValid(nbr)) {
                continue;
            }

            // Search all enemy players for units on this tile
            for (const std::unique_ptr<aoc::game::Player>& otherPlayer : gameState.players()) {
                if (otherPlayer->id() == player) { continue; }

                aoc::game::Unit* target = otherPlayer->unitAt(nbr);
                if (target == nullptr) { continue; }
                if (!target->isMilitary()) { continue; }

                // Compute bombardment damage
                float defenseStrength = static_cast<float>(target->combatStrength());
                float ratio = bombardStrength / std::max(defenseStrength, 0.01f);
                float randomFactor = 0.8f + rng.nextFloat() * 0.4f;
                float baseDamage = 30.0f * ratio * randomFactor;
                int32_t damage = std::clamp(static_cast<int32_t>(baseDamage), 0, 100);

                target->takeDamage(damage);

                LOG_INFO("City %s bombarded %.*s at (%d,%d) for %d damage (HP: %d)",
                         city->name().c_str(),
                         static_cast<int>(target->typeDef().name.size()),
                         target->typeDef().name.data(),
                         nbr.q, nbr.r, damage, target->hitPoints());

                if (target->isDead()) {
                    otherPlayer->removeUnit(target);
                    LOG_INFO("City bombardment destroyed enemy unit");
                }

                break;  // One target per neighbor tile
            }
        }
    }
}

} // namespace aoc::sim
