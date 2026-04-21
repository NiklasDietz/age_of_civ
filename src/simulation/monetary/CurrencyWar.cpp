/**
 * @file CurrencyWar.cpp
 * @brief Competitive devaluation and currency war mechanics.
 */

#include "aoc/simulation/monetary/CurrencyWar.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/core/Log.hpp"

namespace aoc::sim {

constexpr int32_t DEVALUATION_DURATION       = 5;
constexpr float   DEVALUATION_EXPORT_BONUS   = 0.15f;
constexpr float   DEVALUATION_IMPORT_PENALTY = 0.15f;
constexpr float   DEVALUATION_MONEY_INCREASE = 0.20f;
constexpr int32_t RACE_TO_BOTTOM_THRESHOLD   = 3;
constexpr int32_t RACE_TO_BOTTOM_DURATION    = 10;
constexpr float   RACE_TO_BOTTOM_TRADE_MULT  = 0.80f;

ErrorCode devalueCurrency(aoc::game::GameState& /*gameState*/,
                          MonetaryStateComponent& state,
                          CurrencyDevaluationComponent& deval,
                          const GlobalCurrencyWarState& global) {
    if (state.system != MonetarySystemType::FiatMoney
        && state.system != MonetarySystemType::Digital) {
        return ErrorCode::InvalidMonetaryTransition;
    }
    if (deval.isDevalued) {
        return ErrorCode::InvalidArgument;  // Already devaluing
    }
    if (global.stabilityPactActive) {
        return ErrorCode::InvalidArgument;  // Pact prohibits devaluation
    }

    // Increase money supply by 20%
    CurrencyAmount increase = static_cast<CurrencyAmount>(
        static_cast<float>(state.moneySupply) * DEVALUATION_MONEY_INCREASE);
    state.moneySupply += increase;
    state.treasury    += increase;

    // Activate devaluation effects
    deval.isDevalued           = true;
    deval.devaluationTurnsLeft = DEVALUATION_DURATION;
    deval.exportBonus          = DEVALUATION_EXPORT_BONUS;
    deval.importPenalty        = DEVALUATION_IMPORT_PENALTY;
    ++deval.devaluationCount;

    LOG_INFO("Player %u: currency devaluation! Money supply +20%%, exports -15%%, imports +15%%",
             static_cast<unsigned>(state.owner));

    return ErrorCode::Ok;
}

void processCurrencyWar(aoc::game::GameState& gameState, GlobalCurrencyWarState& global) {
    // Tick down individual devaluations and count active ones
    int32_t activeDevaluations = 0;
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) {
            continue;
        }
        CurrencyDevaluationComponent& deval = playerPtr->currencyDevaluation();
        if (deval.isDevalued) {
            --deval.devaluationTurnsLeft;
            if (deval.devaluationTurnsLeft <= 0) {
                deval.isDevalued    = false;
                deval.exportBonus   = 0.0f;
                deval.importPenalty = 0.0f;
                LOG_INFO("Player %u: devaluation expired",
                         static_cast<unsigned>(playerPtr->id()));
            } else {
                ++activeDevaluations;
            }
        }
    }

    // Check for race-to-bottom trigger
    if (!global.isRaceToBottom && activeDevaluations >= RACE_TO_BOTTOM_THRESHOLD) {
        global.isRaceToBottom      = true;
        global.raceToBottomTurns   = RACE_TO_BOTTOM_DURATION;
        global.globalTradeMultiplier = RACE_TO_BOTTOM_TRADE_MULT;
        LOG_INFO("RACE TO THE BOTTOM: %d civs devaluing simultaneously! "
                 "Global trade -20%% for %d turns",
                 activeDevaluations, RACE_TO_BOTTOM_DURATION);
    }

    // Tick down race-to-bottom
    if (global.isRaceToBottom) {
        --global.raceToBottomTurns;
        if (global.raceToBottomTurns <= 0) {
            global.isRaceToBottom        = false;
            global.globalTradeMultiplier = 1.0f;
            LOG_INFO("Race to the bottom ended, global trade restored");
        }
    }
}

} // namespace aoc::sim
