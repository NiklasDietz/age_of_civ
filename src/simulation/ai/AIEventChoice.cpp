/**
 * @file AIEventChoice.cpp
 * @brief Gene-driven AI choice selection for pending WorldEvents.
 */

#include "aoc/simulation/ai/AIEventChoice.hpp"

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/ai/LeaderPersonality.hpp"
#include "aoc/core/Log.hpp"

#include <limits>

namespace aoc::sim::ai {

float scoreEventChoice(const aoc::game::GameState& gameState,
                       PlayerId player,
                       const aoc::sim::EventChoice& choice) {
    const aoc::game::Player* playerObj = gameState.player(player);
    if (playerObj == nullptr) { return 0.0f; }

    const LeaderBehavior& behavior = leaderPersonality(playerObj->civId()).behavior;

    float score = 0.0f;

    // Gold: value scales with economicFocus; 1 gold ~= 1 score unit at focus=1.0.
    score += static_cast<float>(choice.goldChange) * behavior.economicFocus;

    // Production / science / culture: percent-based timed effects. Treat each
    // percentage point as ~0.5 utility, multiplied by focus gene and duration.
    const float durationWeight =
        (choice.effectDuration > 0)
        ? static_cast<float>(choice.effectDuration) * 0.5f
        : 1.0f;
    score += static_cast<float>(choice.productionChange)
           * behavior.prodBuildings * durationWeight * 0.5f;
    score += static_cast<float>(choice.scienceChange)
           * behavior.scienceFocus * durationWeight * 0.5f;
    score += static_cast<float>(choice.cultureChange)
           * behavior.cultureFocus * durationWeight * 0.5f;

    // Happiness and loyalty: periphery-tolerant leaders ignore minor loyalty hits
    // because they're used to far-flung cities. Risk-averse leaders care more.
    score += choice.happinessChange * 20.0f * (2.0f - behavior.riskTolerance);
    score += choice.loyaltyChange * 25.0f * (2.0f - behavior.peripheryTolerance);

    // Population loss: risk-averse leaders heavily penalise casualties.
    if (choice.populationChange < 0) {
        score += static_cast<float>(choice.populationChange) * 30.0f
               * (2.0f - behavior.riskTolerance);
    } else {
        score += static_cast<float>(choice.populationChange) * 15.0f;
    }

    return score;
}

int32_t chooseEventChoice(const aoc::game::GameState& gameState,
                           PlayerId player,
                           const aoc::sim::WorldEventDef& eventDef) {
    if (eventDef.choiceCount <= 0) { return 0; }

    int32_t bestIdx = 0;
    float bestScore = -std::numeric_limits<float>::infinity();
    for (int32_t i = 0; i < eventDef.choiceCount; ++i) {
        const float s = scoreEventChoice(gameState, player, eventDef.choices[i]);
        if (s > bestScore) {
            bestScore = s;
            bestIdx = i;
        }
    }
    return bestIdx;
}

void resolvePendingAIEvents(aoc::game::GameState& gameState) {
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        aoc::game::Player* playerObj = playerPtr.get();
        if (playerObj == nullptr || playerObj->isHuman()) { continue; }

        PlayerEventComponent& events = playerObj->events();
        if (events.pendingEvent == static_cast<WorldEventId>(255)) { continue; }

        const WorldEventDef& eventDef = worldEventDef(events.pendingEvent);
        const int32_t choice = chooseEventChoice(gameState, playerObj->id(), eventDef);
        LOG_INFO("AI %u event choice: event=%u pick=%d",
                 static_cast<unsigned>(playerObj->id()),
                 static_cast<unsigned>(events.pendingEvent),
                 choice);
        const ErrorCode ec = resolveWorldEvent(gameState, playerObj->id(), choice);
        if (ec != ErrorCode::Ok) {
            LOG_INFO("AI %u failed to resolve pending world event (code %d)",
                     static_cast<unsigned>(playerObj->id()),
                     static_cast<int>(ec));
        }
    }
}

} // namespace aoc::sim::ai
