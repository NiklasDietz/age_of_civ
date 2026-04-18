/**
 * @file AIInvestmentController.cpp
 * @brief Implementation of gene-driven AI stock-market decisions.
 */

#include "aoc/simulation/ai/AIInvestmentController.hpp"
#include "aoc/simulation/ai/LeaderPersonality.hpp"
#include "aoc/simulation/economy/StockMarket.hpp"
#include "aoc/simulation/economy/SpeculationBubble.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
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

        // Stock market requires Gold Standard+. Skip pre-industrial civs to
        // avoid spamming InvalidMonetaryTransition rejections.
        if (investor->monetary().system < MonetarySystemType::GoldStandard) {
            continue;
        }

        const CurrencyAmount treasury = investor->treasury();
        // Lowered from 500 to 200: small/mid economies have plausible idle
        // gold above 200 mid-game. At 500 most runs never triggered.
        if (treasury < 200) { continue; }

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

        // investInEconomy rejects self-investment. Economic leaders with spare
        // cash instead back a foreign economy; they get the same dividend flow
        // without the API contract violation.
        const float minAppetite = (bh.economicFocus > 1.2f) ? 0.5f : 1.0f;
        if (bh.speculationAppetite < minAppetite) { continue; }
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
        ErrorCode ec =
            investInEconomy(gameState, investor->id(), best->id(), investSize);
        LOG_INFO("AI %u invest FOREIGN p%u amt=%d (bubble=%d, ec=%d)",
                 static_cast<unsigned>(investor->id()),
                 static_cast<unsigned>(best->id()),
                 static_cast<int>(investSize),
                 static_cast<int>(bubble.phase),
                 static_cast<int>(ec));
    }
}

} // namespace aoc::sim::ai
