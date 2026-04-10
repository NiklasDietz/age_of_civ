/**
 * @file SpeculationBubble.cpp
 * @brief Asset speculation bubble lifecycle and crash mechanics.
 */

#include "aoc/simulation/economy/SpeculationBubble.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim {

void processSpeculationBubble(PlayerBubbleComponent& bubble,
                               CurrencyAmount currentGDP,
                               float interestRate,
                               bool hasNegativeShock) {
    ++bubble.phaseTimer;

    // Calculate GDP growth rate
    float growthRate = 0.0f;
    if (bubble.previousGDP > 0) {
        growthRate = static_cast<float>(currentGDP - bubble.previousGDP)
                   / static_cast<float>(bubble.previousGDP);
    }
    bubble.previousGDP = currentGDP;

    switch (bubble.phase) {
        case BubblePhase::None: {
            // Track consecutive growth turns
            if (growthRate > 0.03f) {
                ++bubble.growthStreak;
            } else {
                bubble.growthStreak = std::max(0, bubble.growthStreak - 1);
            }

            // Bubble forms after 10+ turns of strong growth AND low interest rates
            if (bubble.growthStreak >= 10 && interestRate < 0.08f) {
                bubble.phase = BubblePhase::Formation;
                bubble.phaseTimer = 0;
                bubble.bubbleMagnitude = 1.0f;
                LOG_INFO("Player %u: speculation bubble FORMING (growth streak: %d)",
                         static_cast<unsigned>(bubble.owner), bubble.growthStreak);
            }
            break;
        }

        case BubblePhase::Formation: {
            // Bubble inflates 3-8% per turn
            float inflationRate = 0.03f + growthRate * 0.5f;
            // High interest rates slow bubble formation
            inflationRate -= interestRate * 0.5f;
            inflationRate = std::max(0.0f, inflationRate);

            bubble.bubbleMagnitude += inflationRate;

            // Transition to Inflation phase after 5 turns
            if (bubble.phaseTimer >= 5) {
                bubble.phase = BubblePhase::Inflation;
                bubble.phaseTimer = 0;
                LOG_INFO("Player %u: bubble INFLATING (magnitude: %.2f)",
                         static_cast<unsigned>(bubble.owner),
                         static_cast<double>(bubble.bubbleMagnitude));
            }

            // High rates can kill the bubble early
            if (interestRate >= 0.15f) {
                bubble.phase = BubblePhase::None;
                bubble.bubbleMagnitude = 1.0f;
                bubble.growthStreak = 0;
                LOG_INFO("Player %u: bubble deflated by high interest rates",
                         static_cast<unsigned>(bubble.owner));
            }
            break;
        }

        case BubblePhase::Inflation: {
            // Bubble grows faster during inflation phase
            float inflationRate = 0.05f + growthRate * 0.8f;
            inflationRate -= interestRate * 0.3f;
            inflationRate = std::max(0.01f, inflationRate);
            bubble.bubbleMagnitude += inflationRate;

            // Transition to Euphoria at 1.5x magnitude
            if (bubble.bubbleMagnitude >= 1.50f) {
                bubble.phase = BubblePhase::Euphoria;
                bubble.phaseTimer = 0;
                LOG_INFO("Player %u: bubble reaching EUPHORIA (magnitude: %.2f)",
                         static_cast<unsigned>(bubble.owner),
                         static_cast<double>(bubble.bubbleMagnitude));
            }

            // Check for pop
            if (hasNegativeShock || interestRate >= 0.20f) {
                bubble.phase = BubblePhase::Crash;
                bubble.phaseTimer = 0;
                LOG_INFO("Player %u: bubble POPPED during inflation! (magnitude was %.2f)",
                         static_cast<unsigned>(bubble.owner),
                         static_cast<double>(bubble.bubbleMagnitude));
            }
            break;
        }

        case BubblePhase::Euphoria: {
            // Peak bubble: still growing but extremely fragile
            bubble.bubbleMagnitude += 0.02f;
            bubble.bubbleMagnitude = std::min(bubble.bubbleMagnitude, 2.50f);

            // ANY negative shock pops the bubble during euphoria
            // Also pops if sustained too long (max 15 turns)
            bool naturalPop = (bubble.phaseTimer >= 15);
            bool shockPop = hasNegativeShock;
            bool ratePop = (interestRate >= 0.12f);
            bool gdpSlowdown = (growthRate < 0.01f);

            if (naturalPop || shockPop || ratePop || gdpSlowdown) {
                bubble.phase = BubblePhase::Crash;
                bubble.phaseTimer = 0;
                LOG_INFO("Player %u: BUBBLE CRASH! Magnitude was %.2f, trigger: %s",
                         static_cast<unsigned>(bubble.owner),
                         static_cast<double>(bubble.bubbleMagnitude),
                         shockPop ? "shock" : (ratePop ? "rates" : (gdpSlowdown ? "slowdown" : "natural")));
            }
            break;
        }

        case BubblePhase::Crash: {
            // Crash lasts 8 turns, magnitude decays toward 1.0
            bubble.bubbleMagnitude *= 0.85f;  // 15% value loss per turn
            if (bubble.bubbleMagnitude < 1.0f) { bubble.bubbleMagnitude = 1.0f; }

            if (bubble.phaseTimer >= 8) {
                bubble.phase = BubblePhase::Recovery;
                bubble.phaseTimer = 0;
                LOG_INFO("Player %u: entering post-crash RECOVERY",
                         static_cast<unsigned>(bubble.owner));
            }
            break;
        }

        case BubblePhase::Recovery: {
            // Recovery takes 15 turns, gradually returns to normal
            if (bubble.phaseTimer >= 15) {
                bubble.phase = BubblePhase::None;
                bubble.phaseTimer = 0;
                bubble.bubbleMagnitude = 1.0f;
                bubble.growthStreak = 0;
                LOG_INFO("Player %u: RECOVERED from bubble crash",
                         static_cast<unsigned>(bubble.owner));
            }
            break;
        }

        default:
            break;
    }
}

} // namespace aoc::sim
