#pragma once

/**
 * @file WarState.hpp
 * @brief Active war tracking, war score, and peace deal terms.
 */

#include "aoc/core/Types.hpp"

#include <cstdint>
#include <vector>

namespace aoc::sim {

enum class CasusBelli : uint8_t {
    Unprovoked,          ///< No justification (large diplomatic penalty)
    TerritorialDispute,  ///< Border conflict
    TradeSanction,       ///< Retaliation for embargo
    AllianceDefense,     ///< Honoring a defensive pact
    Liberation,          ///< Freeing occupied cities
};

struct ActiveWar {
    PlayerId    aggressor;
    PlayerId    defender;
    CasusBelli  casusBelli;
    TurnNumber  startTurn;
    int32_t     aggressorWarScore = 0;  ///< From battles won, cities captured
    int32_t     defenderWarScore  = 0;
};

struct PeaceTerms {
    bool             cede_cities = false;
    CurrencyAmount   reparations = 0;     ///< Gold payment from loser
    int32_t          truceLength = 10;    ///< Turns of forced peace
};

/// ECS component tracking all active wars for a player.
struct PlayerWarComponent {
    PlayerId owner = INVALID_PLAYER;
    std::vector<ActiveWar> activeWars;

    void addWarScore(PlayerId enemy, int32_t score) {
        for (ActiveWar& war : this->activeWars) {
            if (war.aggressor == enemy || war.defender == enemy) {
                if (war.aggressor == this->owner) {
                    war.aggressorWarScore += score;
                } else {
                    war.defenderWarScore += score;
                }
                return;
            }
        }
    }
};

} // namespace aoc::sim
