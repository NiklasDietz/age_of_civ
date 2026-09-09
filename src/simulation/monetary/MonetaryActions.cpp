/**
 * @file MonetaryActions.cpp
 * @brief Validated player actions on the monetary system. See the header for
 *        why these exist.
 */

#include "aoc/simulation/monetary/MonetaryActions.hpp"

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/monetary/CentralBank.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include <algorithm>
#include "aoc/simulation/monetary/CurrencyCrisis.hpp"
#include "aoc/simulation/economy/SpeculationBubble.hpp"

namespace aoc::sim {

namespace {

/// Every action here needs the same thing: a real player with monetary state.
[[nodiscard]] aoc::game::Player* actor(aoc::game::GameState& gameState, PlayerId player) {
    if (player >= aoc::sim::CITY_STATE_PLAYER_BASE) { return nullptr; }
    return gameState.player(player);
}

[[nodiscard]] bool isFiatClass(MonetarySystemType s) {
    return s == MonetarySystemType::FiatMoney || s == MonetarySystemType::Digital;
}

} // namespace

/// Systems with a central bank able to set a policy rate. Commodity coinage
/// has no such institution, and Barter has no money to price.
[[nodiscard]] bool hasCentralBank(MonetarySystemType s) {
    return s == MonetarySystemType::GoldStandard || s == MonetarySystemType::FiatMoney
           || s == MonetarySystemType::Digital;
}

// Policy constants. The rate is a genuine tradeoff rather than a dial with a
// best setting, so these are the weights on each side of it.
//
/// Inflation the bank is content with. Above this it tightens.
///
/// MEASURED FROM THIS GAME, not borrowed from macroeconomics. Mean inflation
/// over 500 turns is 0.0014 on seed 42 and 0.0073 on seed 43, so a real-world
/// 2-4% target is unreachably high: the rule then reads every turn as a
/// shortfall, cuts to zero and stays there. Tried it -- rates pegged at the
/// floor all game, which is under the 0.08 bubble threshold, and speculative
/// bubbles went to 25 on seed 42 and 111 on seed 43 while seed 43's revolts
/// rose to 957. A target has to sit inside the distribution it is steering.
constexpr float INFLATION_TARGET = 0.005f;
/// Rate the bank returns to when inflation is on target and debt is light.
constexpr float NEUTRAL_RATE = 0.05f;
/// How hard it leans on the rate per point of inflation overshoot. 1.5 is the
/// Taylor-rule convention: respond by more than the gap, or the response never
/// catches up.
constexpr float INFLATION_RESPONSE = 1.5f;
/// Debt service is debt * rate every turn (FiscalPolicy), so a heavily indebted
/// civ cannot afford to tighten as hard. Measured against GDP.
constexpr float DEBT_RELIEF = 0.04f;
/// Bubbles form only below this rate (SpeculationBubble). A bank already
/// watching one inflate will not cut under it.
constexpr float BUBBLE_FLOOR = 0.08f;
/// Most the rate moves in one turn. Central banks move in steps, and a jumpy
/// rate would whipsaw velocity and every bond priced off it.
constexpr float MAX_STEP = 0.02f;

ErrorCode requestDebaseCurrency(aoc::game::GameState& gameState, PlayerId player, float ratio) {
    aoc::game::Player* p = actor(gameState, player);
    if (p == nullptr) { return ErrorCode::EntityNotFound; }
    if (ratio <= 0.0f) { return ErrorCode::InvalidArgument; }
    // Debasement is a COINAGE act. There is no metal content to dilute once a
    // civ is on paper, and the fiat equivalent is printing.
    if (p->monetary().system != MonetarySystemType::CommodityMoney) {
        return ErrorCode::InvalidState;
    }
    return debaseCurrency(p->monetary(), ratio);
}

ErrorCode requestRemintCurrency(aoc::game::GameState& gameState, PlayerId player) {
    aoc::game::Player* p = actor(gameState, player);
    if (p == nullptr) { return ErrorCode::EntityNotFound; }
    if (p->monetary().debasement.debasementRatio <= 0.0f) {
        return ErrorCode::InvalidState; // nothing to put right
    }
    return remintCurrency(p->monetary());
}

ErrorCode requestDevalueCurrency(aoc::game::GameState& gameState, PlayerId player,
                                 GlobalCurrencyWarState& warState) {
    aoc::game::Player* p = actor(gameState, player);
    if (p == nullptr) { return ErrorCode::EntityNotFound; }
    if (!isFiatClass(p->monetary().system)) {
        return ErrorCode::InvalidState; // a metal currency cannot be talked down
    }
    return devalueCurrency(gameState, p->monetary(), p->currencyDevaluation(), warState);
}

ErrorCode requestPrintMoney(aoc::game::GameState& gameState, PlayerId player,
                            CurrencyAmount amount) {
    aoc::game::Player* p = actor(gameState, player);
    if (p == nullptr) { return ErrorCode::EntityNotFound; }
    if (amount <= 0) { return ErrorCode::InvalidArgument; }
    if (!isFiatClass(p->monetary().system)) { return ErrorCode::InvalidState; }
    // printMoney caps at a share of GDP and returns what it actually issued;
    // zero means the cap refused the whole request.
    return (p->monetary().printMoney(amount) > 0) ? ErrorCode::Ok : ErrorCode::InvalidState;
}

ErrorCode requestSetInterestRate(aoc::game::GameState& gameState, PlayerId player, float rate) {
    aoc::game::Player* p = actor(gameState, player);
    if (p == nullptr) { return ErrorCode::EntityNotFound; }
    if (!(rate >= 0.0f) || rate > 1.0f) {
        // Rejects NaN as well: a NaN rate would poison velocity, every bond
        // yield and the exchange rate, and would not be visible as a zero.
        return ErrorCode::InvalidArgument;
    }
    if (!hasCentralBank(p->monetary().system)) { return ErrorCode::InvalidState; }
    setInterestRate(p->monetary(), rate);
    return ErrorCode::Ok;
}

float applyCentralBankPolicy(aoc::game::GameState& gameState, PlayerId player) {
    aoc::game::Player* p = actor(gameState, player);
    if (p == nullptr) { return 0.0f; }

    MonetaryStateComponent& state = p->monetary();
    if (!hasCentralBank(state.system)) { return state.interestRate; }

    // A hyperinflation crisis is already handled, and at maximum: the AI's
    // crisis response slams the rate to 0.25 and zeroes spending. Do not talk
    // it back down mid-emergency.
    if (p->currencyCrisis().activeCrisis == CrisisType::Hyperinflation) {
        return state.interestRate;
    }

    // Lean against inflation...
    const float gap    = state.inflationRate - INFLATION_TARGET;
    float       target = NEUTRAL_RATE + gap * INFLATION_RESPONSE;

    // ...but not past what the debt can carry. Every point of rate costs
    // governmentDebt * rate per turn, so the more a civ owes relative to what
    // it produces, the less tightening it can afford.
    if (state.gdp > 0 && state.governmentDebt > 0) {
        const float debtToGdp = static_cast<float>(state.governmentDebt)
                              / static_cast<float>(state.gdp);
        target -= debtToGdp * DEBT_RELIEF;
    }

    // A bubble inflating is a reason not to be the cheapest money in the room.
    const bool bubbleRisk = p->bubble().growthStreak >= 5;
    if (bubbleRisk && target < BUBBLE_FLOOR) { target = BUBBLE_FLOOR; }

    const float step    = std::clamp(target - state.interestRate, -MAX_STEP, MAX_STEP);
    const float newRate = state.interestRate + step;
    setInterestRate(state, newRate);
    return state.interestRate;
}

} // namespace aoc::sim
