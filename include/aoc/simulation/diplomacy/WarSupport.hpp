#pragma once

/**
 * @file WarSupport.hpp
 * @brief War Support / Surrender system (Humankind-inspired).
 *
 * Each player in a war has War Support (0-100). When it hits 0, that player
 * is forced to surrender. The victor's War Score determines what they can
 * demand in the peace treaty (cities, resources, gold, etc.).
 *
 * War Support changes:
 *   +10 per battle won
 *   -15 per battle lost in own territory
 *   -10 per battle lost in foreign territory
 *   -5 per turn if enemy occupies your cities
 *   +5 per turn if you occupy enemy cities
 *   -3 per turn from war weariness (cumulative)
 *   +20 from Casus Belli (righteous war)
 *
 * War Score (accumulated on victory):
 *   +10 per battle won
 *   +20 per city captured
 *   +5 per unit destroyed
 *
 * Peace terms (spend War Score as currency):
 *   City cession: 30 points per city
 *   Resource tribute: 10 points per resource
 *   Gold tribute: 5 points per 500 gold
 *   Open borders (forced): 15 points
 *   Demilitarize border: 20 points
 */

#include "aoc/core/Types.hpp"

#include <cstdint>

namespace aoc::sim {

/// Per-war tracking between two players.
struct WarSupportState {
    PlayerId player1 = INVALID_PLAYER;
    PlayerId player2 = INVALID_PLAYER;
    int32_t  support1 = 50;  ///< Player 1's war support (0-100)
    int32_t  support2 = 50;  ///< Player 2's war support (0-100)
    int32_t  score1   = 0;   ///< Player 1's accumulated war score
    int32_t  score2   = 0;   ///< Player 2's accumulated war score
    int32_t  turnsAtWar = 0;

    /// Adjust support for a player after a battle.
    void battleResult(PlayerId winner, bool inOwnTerritory) {
        const int32_t winBonus = 10;
        const int32_t lossInOwn = -15;
        const int32_t lossAbroad = -10;

        if (winner == this->player1) {
            this->support1 += winBonus;
            this->support2 += inOwnTerritory ? lossInOwn : lossAbroad;
            this->score1 += 10;
        } else {
            this->support2 += winBonus;
            this->support1 += inOwnTerritory ? lossInOwn : lossAbroad;
            this->score2 += 10;
        }
        this->clamp();
    }

    /// Tick once per turn: war weariness reduces both sides' support.
    void tick() {
        ++this->turnsAtWar;
        const int32_t weariness = std::min(this->turnsAtWar / 5, 10);  // Max -10/turn
        this->support1 -= weariness;
        this->support2 -= weariness;
        this->clamp();
    }

    /// City occupation effects.
    void cityOccupied(PlayerId occupier) {
        if (occupier == this->player1) {
            this->support1 += 5;
            this->support2 -= 5;
            this->score1 += 20;
        } else {
            this->support2 += 5;
            this->support1 -= 5;
            this->score2 += 20;
        }
        this->clamp();
    }

    /// Check if either side is forced to surrender.
    [[nodiscard]] bool player1Surrenders() const { return this->support1 <= 0; }
    [[nodiscard]] bool player2Surrenders() const { return this->support2 <= 0; }

private:
    void clamp() {
        this->support1 = std::max(0, std::min(100, this->support1));
        this->support2 = std::max(0, std::min(100, this->support2));
    }
};

} // namespace aoc::sim
