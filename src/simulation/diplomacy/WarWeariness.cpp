/**
 * @file WarWeariness.cpp
 * @brief War weariness accumulation, decay, and effect computation.
 */

#include "aoc/simulation/diplomacy/WarWeariness.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

void processWarWeariness(aoc::game::Player& player,
                         const DiplomacyManager& diplomacy) {
    PlayerWarWearinessComponent& ww = player.warWeariness();

    bool atWarWithAnyone = false;
    const uint8_t playerCount = diplomacy.playerCount();

    for (uint8_t other = 0; other < playerCount; ++other) {
        if (other == player.id()) {
            continue;
        }

        if (diplomacy.isAtWar(player.id(), other)) {
            atWarWithAnyone = true;
            ww.turnsAtWar[other] += 1;
            constexpr float BASE_WEARINESS_PER_TURN = 1.0f;
            ww.weariness += BASE_WEARINESS_PER_TURN;
        } else {
            ww.turnsAtWar.erase(other);
        }
    }

    if (!atWarWithAnyone) {
        constexpr float PEACE_DECAY = 2.0f;
        ww.weariness -= PEACE_DECAY;
    }

    ww.weariness = std::clamp(ww.weariness, 0.0f, 100.0f);
}

float warWearinessHappinessPenalty(float weariness) {
    if (weariness < 20.0f) {
        return 0.0f;
    }
    if (weariness < 40.0f) {
        return -1.0f;
    }
    if (weariness < 60.0f) {
        return -2.0f;
    }
    if (weariness < 80.0f) {
        return -3.0f;
    }
    return -5.0f;
}

float warWearinessProductionModifier(float weariness) {
    if (weariness < 60.0f) {
        return 1.0f;
    }
    if (weariness < 80.0f) {
        return 0.9f;   // -10%
    }
    return 0.8f;       // -20%
}

float warWearinessCombatModifier(float weariness) {
    if (weariness < 80.0f) {
        return 1.0f;
    }
    return 0.9f;        // -10%
}

} // namespace aoc::sim
