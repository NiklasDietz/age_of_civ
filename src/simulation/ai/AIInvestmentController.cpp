/**
 * @file AIInvestmentController.cpp
 * @brief Implementation of gene-driven AI stock-market decisions.
 */

#include "aoc/simulation/ai/AIInvestmentController.hpp"
#include "aoc/simulation/ai/LeaderPersonality.hpp"
#include "aoc/simulation/economy/StockMarket.hpp"
#include "aoc/simulation/economy/SpeculationBubble.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim::ai {

void runAIInvestmentDecisions(aoc::game::GameState& gameState) {
    for (const std::unique_ptr<aoc::game::Player>& investorPtr : gameState.players()) {
        aoc::game::Player* investor = investorPtr.get();
        if (investor == nullptr || investor->isHuman()) { continue; }

        const LeaderBehavior& bh =
            leaderPersonality(investor->civId()).behavior;

        const CurrencyAmount treasury = investor->treasury();
        if (treasury < 500) { continue; }

        // Bubble gate: during inflated phases, only confident speculators keep
        // buying. Cautious leaders sit out.
        const PlayerBubbleComponent& bubble = investor->bubble();
        const bool bubbleHigh =
            bubble.phase == BubblePhase::Euphoria
            || (bubble.phase == BubblePhase::Inflation && bubble.bubbleMagnitude > 1.4f);
        if (bubbleHigh && bh.speculationAppetite + bh.riskTolerance < 2.5f) {
            continue;
        }
        if (bubble.phase == BubblePhase::Crash) { continue; }

        // Investment size capped at 20% treasury per plan risk mitigation.
        const float sizeRaw = static_cast<float>(treasury)
                            * 0.1f * bh.speculationAppetite;
        const CurrencyAmount investSize = std::min(
            static_cast<CurrencyAmount>(sizeRaw),
            static_cast<CurrencyAmount>(treasury / 5));
        if (investSize < 100) { continue; }

        // Self-investment preference for economic leaders with spare cash.
        if (bh.economicFocus > 1.2f) {
            [[maybe_unused]] ErrorCode ec =
                investInEconomy(gameState, investor->id(), investor->id(), investSize);
            continue;
        }

        // Otherwise look for a reasonable foreign target. Pick the wealthiest
        // non-hostile other player the speculation appetite tolerates.
        if (bh.speculationAppetite < 1.0f) { continue; }
        aoc::game::Player* best = nullptr;
        CurrencyAmount bestTreasury = 0;
        for (const std::unique_ptr<aoc::game::Player>& otherPtr : gameState.players()) {
            aoc::game::Player* other = otherPtr.get();
            if (other == nullptr || other == investor) { continue; }
            if (other->treasury() > bestTreasury) {
                best = other;
                bestTreasury = other->treasury();
            }
        }
        if (best == nullptr) { continue; }
        [[maybe_unused]] ErrorCode ec =
            investInEconomy(gameState, investor->id(), best->id(), investSize);
    }
}

} // namespace aoc::sim::ai
