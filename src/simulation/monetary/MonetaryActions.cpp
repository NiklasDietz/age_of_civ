/**
 * @file MonetaryActions.cpp
 * @brief Validated player actions on the monetary system. See the header for
 *        why these exist.
 */

#include "aoc/simulation/monetary/MonetaryActions.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/ai/AIConstants.hpp"
#include "aoc/simulation/diplomacy/DealTerms.hpp"
#include "aoc/simulation/economy/SpeculationBubble.hpp"
#include "aoc/simulation/economy/TradeRouteSystem.hpp"
#include "aoc/simulation/monetary/CentralBank.hpp"
#include "aoc/simulation/monetary/CurrencyCrisis.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/monetary/MoneyFlow.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"

#include "aoc/core/Log.hpp"

#include <algorithm>
#include <array>
#include <string>

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

constexpr TechId TECH_PRINTING{55};
constexpr TechId TECH_ECONOMICS{13};
constexpr int32_t GOLD_BARS_FOR_A_GOLD_STANDARD = 3;

[[nodiscard]] bool hasMint(const aoc::game::Player& player) {
    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        if (city != nullptr && city->owner() == player.id() && city->hasBuilding(ai::BUILDING_MINT)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] int32_t mintedOf(const MonetaryStateComponent& state, CoinTier tier) {
    switch (tier) {
        case CoinTier::Copper: return state.copperCoinReserves;
        case CoinTier::Silver: return state.silverCoinReserves;
        case CoinTier::Gold:   return state.goldBarReserves;
        default:               return 0;
    }
}

} // namespace

int32_t livePartnerCount(const aoc::game::GameState& gameState, PlayerId player) {
    const aoc::game::Player* me = gameState.player(player);
    if (me == nullptr) {
        return 0;
    }
    std::array<bool, MAX_PLAYERS> seen{};
    int32_t count = 0;
    const auto mark = [&](PlayerId other) {
        if (other != player && other < MAX_PLAYERS && !seen[other]) {
            seen[other] = true;
            ++count;
        }
    };
    for (const std::unique_ptr<aoc::game::Unit>& unit : me->units()) {
        if (unitTypeDef(unit->typeId()).unitClass != UnitClass::Trader) {
            continue;
        }
        const TraderComponent& trader = unit->trader();
        if (trader.owner != INVALID_PLAYER && trader.destOwner != INVALID_PLAYER) {
            mark(trader.destOwner);
        }
    }
    for (const DiplomaticDeal& deal : gameState.deals().activeDeals) {
        if (!deal.isAccepted || deal.isBroken || (deal.playerA != player && deal.playerB != player)) {
            continue;
        }
        for (const DealTerm& term : deal.terms) {
            if (term.type == DealTermType::SupplyContract && term.duration > 0) {
                mark(deal.playerA == player ? deal.playerB : deal.playerA);
                break;
            }
        }
    }
    return count;
}

CoinTier preferredCoinTier(const MonetaryStateComponent& state) {
    if (state.goldBarReserves >= GOLD_BARS_FOR_A_GOLD_STANDARD) {
        return CoinTier::Gold;
    }
    if (state.silverCoinReserves > 0 && state.silverCoinReserves >= state.copperCoinReserves) {
        return CoinTier::Silver;
    }
    if (state.copperCoinReserves > 0) {
        return CoinTier::Copper;
    }
    return CoinTier::None;
}

bool coinageWithinReach(const aoc::game::GameState& gameState, PlayerId player) {
    const aoc::game::Player* p = player < CITY_STATE_PLAYER_BASE ? gameState.player(player) : nullptr;
    if (p == nullptr || p->monetary().system != MonetarySystemType::Barter) {
        return false;
    }
    const MonetaryStateComponent& state = p->monetary();
    // Recorded so the gate dump's denominator is every Barter player-turn, not
    // just the ones that reached the table: these two return before it.
    if (!hasMint(*p)) {
        recordGateRefusal(MonetarySystemType::CommodityMoney, GateRefusal::NoMint);
        return false;
    }
    if (state.bullion <= 0 || preferredCoinTier(state) == CoinTier::None) {
        recordGateRefusal(MonetarySystemType::CommodityMoney, GateRefusal::NoBullion);
        return false;
    }
    return state.canTransition(MonetarySystemType::CommodityMoney, p->ownedCityCount(),
                               [p](TechId t) { return p->hasResearched(t); }) == ErrorCode::Ok;
}

ErrorCode requestSetMonetaryRegime(aoc::game::GameState& gameState, PlayerId player,
                                   MonetarySystemType target, CoinTier tier) {
    aoc::game::Player* p = actor(gameState, player);
    if (p == nullptr) { return ErrorCode::EntityNotFound; }
    MonetaryStateComponent& state = p->monetary();
    if (target >= MonetarySystemType::Count ||
        static_cast<uint8_t>(target) != static_cast<uint8_t>(state.system) + 1u) {
        recordGateRefusal(target, GateRefusal::NotNextStage);
        return ErrorCode::InvalidMonetaryTransition; // not the next stage, or already there
    }
    if (target == MonetarySystemType::CommodityMoney) {
        if (tier == CoinTier::None || tier > CoinTier::Gold) {
            recordGateRefusal(target, GateRefusal::BadTier);
            return ErrorCode::InvalidArgument;
        }
        if (!hasMint(*p)) {
            recordGateRefusal(target, GateRefusal::NoMint);
            return ErrorCode::InvalidState;
        }
        if (state.bullion <= 0 || mintedOf(state, tier) <= 0) {
            recordGateRefusal(target, GateRefusal::NoBullion);
            return ErrorCode::InsufficientResources;
        }
    }
    if (target == MonetarySystemType::FiatMoney && !p->hasResearched(TECH_PRINTING) &&
        !p->hasResearched(TECH_ECONOMICS)) {
        recordGateRefusal(target, GateRefusal::PaperTech);
        return ErrorCode::InvalidMonetaryTransition; // paper needs a press or the theory
    }
    const ErrorCode gate = state.canTransition(
        target, p->ownedCityCount(), [p](TechId t) { return p->hasResearched(t); },
        livePartnerCount(gameState, player));
    if (gate != ErrorCode::Ok) { return gate; }

    if (target == MonetarySystemType::CommodityMoney) {
        // The metal held back for this day becomes the people's coin; the
        // standard is fixed from here on.
        state.privateSpecie += state.bullion;
        state.bullion         = 0;
        state.coinageStandard = tier;
        state.updateCoinTier();
    }
    if (target == MonetarySystemType::GoldStandard) {
        // Convertible notes, issued one for one against the people's coin:
        // paper money created, and booked as printed.
        const CurrencyAmount issued = static_cast<CurrencyAmount>(
            static_cast<float>(std::max<CurrencyAmount>(0, state.privateSpecie)) * GOLD_STANDARD_NOTE_ISSUE);
        state.privateNotes += issued;
        if (p->moneyLedger() != nullptr) {
            p->moneyLedger()->record(player, MoneyFlow::printed(), issued);
        }
    }
    state.transitionTo(target);
    state.moneySupply = state.treasury + state.privateSpecie + state.privateNotes;
    LOG_INFO("Player %u adopted %.*s%s", static_cast<unsigned>(player),
             static_cast<int>(monetarySystemName(target).size()), monetarySystemName(target).data(),
             target == MonetarySystemType::CommodityMoney
                 ? (std::string(" on the ") + std::string(coinTierName(tier)) + " standard").c_str()
                 : "");
    return ErrorCode::Ok;
}

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
    return remintCurrency(*p);
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
    const CurrencyAmount issued = p->monetary().printMoney(amount);
    if (issued <= 0) { return ErrorCode::InvalidState; }
    p->addGold(issued, aoc::sim::MoneyFlow::printed());
    return ErrorCode::Ok;
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


int32_t saleability(const SaleabilityInputs& inputs) {
    if (inputs.held <= 0) {
        return 0; // a civ cannot monetise what it does not hold
    }
    // Holding the good is a gate first and only a mild preference after. A civ
    // with forty units and one with a thousand can both price in it, so the term
    // spans 61 to 100 rather than the full range. Letting abundance dominate is
    // precisely preferredCoinTier's failing: it reads reserves alone, so every
    // civ on every seed measured landed on copper, which is merely what gets
    // mined most. Acceptance has to be able to beat abundance, or nothing ever
    // converges on anything but the commonest ore.
    const int32_t stock = 60 + std::min(40, inputs.held);

    // The network term. A good is money because others take it, so acceptance
    // compounds and civs converge. With no contact at all it is neutral rather
    // than zero, or a civ that has met nobody could never adopt anything.
    const int32_t acceptance =
        inputs.totalWeight > 0
            ? 100 + (150 * std::min(inputs.acceptingWeight, inputs.totalWeight)) / inputs.totalWeight
            : 100;

    // What industry eats, the people cannot hoard. Early this is near 100 for a
    // metal nothing consumes; when industrial uses arrive it collapses, and that
    // is what makes paper worth adopting rather than a scripted event.
    const int32_t industrial =
        std::max(0, 100 - (100 * std::max(0, inputs.industrialDraw)) / std::max(1, inputs.held));

    // Stable value is the whole point of holding money rather than goods.
    const int32_t stability =
        std::max(20, 100 - (100 * std::max(0, inputs.priceSwing)) / std::max(1, inputs.price));

    const int64_t score = static_cast<int64_t>(stock) * acceptance * industrial * stability;
    return static_cast<int32_t>(score / (100 * 100 * 100));
}

} // namespace aoc::sim
