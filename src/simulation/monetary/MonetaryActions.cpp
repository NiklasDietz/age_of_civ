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
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/economy/SpeculationBubble.hpp"
#include "aoc/simulation/economy/TradeRouteSystem.hpp"
#include "aoc/simulation/monetary/CentralBank.hpp"
#include "aoc/simulation/monetary/CurrencyCrisis.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/monetary/MoneyFlow.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp" // GOOD_COUNT
#include "aoc/simulation/unit/UnitTypes.hpp"

#include "aoc/core/Log.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <string>

namespace aoc::sim {

namespace {

/// Every action here needs the same thing: a real player with monetary state.
[[nodiscard]] aoc::game::Player* actor(aoc::game::GameState& gameState, PlayerId player) {
    if (player >= aoc::sim::CITY_STATE_PLAYER_BASE) {
        return nullptr;
    }
    return gameState.player(player);
}

[[nodiscard]] const aoc::game::Player* actorConst(const aoc::game::GameState& gameState,
                                                  PlayerId player) {
    if (player >= aoc::sim::CITY_STATE_PLAYER_BASE) {
        return nullptr;
    }
    return gameState.player(player);
}

[[nodiscard]] bool isFiatClass(MonetarySystemType s) {
    return s == MonetarySystemType::FiatMoney || s == MonetarySystemType::Digital;
}

constexpr TechId TECH_PRINTING{55};
constexpr TechId TECH_ECONOMICS{13};

} // namespace

int32_t livePartnerCount(const aoc::game::GameState& gameState, PlayerId player) {
    const aoc::game::Player* me = gameState.player(player);
    if (me == nullptr) {
        return 0;
    }
    std::array<bool, MAX_PLAYERS> seen{};
    int32_t count   = 0;
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
        if (!deal.isAccepted || deal.isBroken ||
            (deal.playerA != player && deal.playerB != player)) {
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

bool coinageWithinReach(const aoc::game::GameState& gameState, PlayerId player) {
    const aoc::game::Player* p =
        player < CITY_STATE_PLAYER_BASE ? gameState.player(player) : nullptr;
    if (p == nullptr || p->monetary().system != MonetarySystemType::Barter) {
        return false;
    }
    // Phase B: canTransition checks privateSpecie >= threshold; no Mint or
    // bullion check needed (coin layer removed).
    return p->monetary().canTransition(MonetarySystemType::CommodityMoney, p->ownedCityCount(),
                                       [p](TechId t) { return p->hasResearched(t); }) ==
           ErrorCode::Ok;
}

ErrorCode requestSetMonetaryRegime(aoc::game::GameState& gameState, PlayerId player,
                                   MonetarySystemType target) {
    aoc::game::Player* p = actor(gameState, player);
    if (p == nullptr) {
        return ErrorCode::EntityNotFound;
    }
    MonetaryStateComponent& state = p->monetary();
    if (target >= MonetarySystemType::Count ||
        static_cast<uint8_t>(target) != static_cast<uint8_t>(state.system) + 1u) {
        recordGateRefusal(target, GateRefusal::NotNextStage);
        return ErrorCode::InvalidMonetaryTransition; // not the next stage, or already there
    }
    if (target == MonetarySystemType::FiatMoney && !p->hasResearched(TECH_PRINTING) &&
        !p->hasResearched(TECH_ECONOMICS)) {
        recordGateRefusal(target, GateRefusal::PaperTech);
        return ErrorCode::InvalidMonetaryTransition; // paper needs a press or the theory
    }
    const ErrorCode gate = state.canTransition(
        target, p->ownedCityCount(), [p](TechId t) { return p->hasResearched(t); },
        livePartnerCount(gameState, player));
    if (gate != ErrorCode::Ok) {
        return gate;
    }

    if (target == MonetarySystemType::CommodityMoney) {
        // Bullion becomes private specie on adoption.
        state.privateSpecie += state.bullion;
        state.bullion = 0;
    }
    if (target == MonetarySystemType::GoldStandard) {
        // Convertible notes, issued one for one against the people's coin:
        // paper money created, and booked as printed.
        const CurrencyAmount issued = static_cast<CurrencyAmount>(
            static_cast<float>(std::max<CurrencyAmount>(0, state.privateSpecie)) *
            GOLD_STANDARD_NOTE_ISSUE);
        state.privateNotes += issued;
        if (p->moneyLedger() != nullptr) {
            p->moneyLedger()->record(player, MoneyFlow::printed(), issued);
        }
    }
    state.transitionTo(target);
    state.moneySupply = state.treasury + state.privateSpecie + state.privateNotes;
    LOG_INFO("Player %u adopted %.*s", static_cast<unsigned>(player),
             static_cast<int>(monetarySystemName(target).size()),
             monetarySystemName(target).data());
    return ErrorCode::Ok;
}

int32_t civHeldUnits(const aoc::game::GameState& gameState, PlayerId player, uint16_t goodId) {
    const aoc::game::Player* p =
        player < aoc::sim::CITY_STATE_PLAYER_BASE ? gameState.player(player) : nullptr;
    if (p == nullptr) {
        return 0;
    }
    int32_t held = 0;
    for (const std::unique_ptr<aoc::game::City>& city : p->cities()) {
        // A revolted city stays in the old holder's vector with its owner
        // cleared, so its stock is no longer this civ's to price in.
        if (city == nullptr || city->owner() != player) {
            continue;
        }
        held += city->stockpile().getAmount(goodId);
    }
    return held;
}

ErrorCode requestSetMoneyGood(aoc::game::GameState& gameState, PlayerId player, uint8_t goodId) {
    aoc::game::Player* p = actor(gameState, player);
    if (p == nullptr) {
        return ErrorCode::EntityNotFound;
    }
    MonetaryStateComponent& state = p->monetary();

    // Every refusal below returns before touching `state`, which is the gate
    // the plan names: a denied request must leave the civ exactly as it was.
    if (state.turnsWithCurrentMoneyGood < MONEY_GOOD_DWELL_TURNS) {
        return ErrorCode::InvalidMoneyGood; // too soon to re-price everything
    }
    if (goodId == state.moneyGood) {
        return ErrorCode::InvalidMoneyGood; // already this civ's money
    }
    if (goodId != NO_MONEY_GOOD) {
        if (goodId >= aoc::sim::goods::GOOD_COUNT) {
            return ErrorCode::InvalidMoneyGood;
        }
        // Under paper the note IS the money, so a commodity cannot also be it.
        // Demonetising (NO_MONEY_GOOD) stays open, which is why this sits
        // inside the branch.
        if (isFiatClass(state.system)) {
            return ErrorCode::InvalidMoneyGood;
        }
        if (civHeldUnits(gameState, player, goodId) <= 0) {
            return ErrorCode::InvalidMoneyGood; // cannot price in what you lack
        }
    }

    state.moneyGood                 = goodId;
    state.turnsWithCurrentMoneyGood = 0;
    if (goodId == NO_MONEY_GOOD) {
        LOG_INFO("Player %u demonetised its money good", static_cast<unsigned>(player));
    } else {
        LOG_INFO("Player %u now prices in good %u", static_cast<unsigned>(player),
                 static_cast<unsigned>(goodId));
    }
    return ErrorCode::Ok;
}

/// How far ahead a challenger must score before it unseats the incumbent
/// money, as a percentage of the incumbent's score. Money is a convention: if
/// a civ re-elected on every marginal tick, nothing would ever stay money long
/// enough for anyone else to start accepting it, and the acceptance term could
/// never compound. The dwell in requestSetMoneyGood bounds how OFTEN a switch
/// can happen; this bounds how SMALL a reason is enough.
constexpr int32_t MONEY_CHALLENGER_MARGIN_PCT = 25;

void aiChooseMoneyGood(aoc::game::GameState& gameState, const Market& market,
                       const DiplomacyManager* diplomacy, PlayerId player) {
    const aoc::game::Player* me = actorConst(gameState, player);
    if (me == nullptr) {
        return;
    }
    const MonetaryStateComponent& state = me->monetary();
    // Under paper the note is the money; there is nothing to elect.
    if (isFiatClass(state.system)) {
        return;
    }
    if (state.turnsWithCurrentMoneyGood < MONEY_GOOD_DWELL_TURNS) {
        return; // still locked in; do not even score
    }

    const MoneyWorldView view = moneyWorldView(gameState, diplomacy, player);
    // Money is for paying others. Before first contact the acceptance term is a
    // fiction and the only stock is the starter kit, so every civ measured
    // elected copper on turn 2 and acceptance then made copper immovable for
    // the whole game. The human may still elect anything through the request.
    if (view.totalWeight == 0) {
        return;
    }

    // AOC_DUMP_MONEY_SCORE: every held good's terms on each scoring turn, so a
    // surprising election can be read off its inputs instead of argued about.
    static const bool dumpScore = std::getenv("AOC_DUMP_MONEY_SCORE") != nullptr;

    int32_t bestScore      = 0;
    uint16_t bestGood      = NO_MONEY_GOOD;
    int32_t incumbentScore = 0;
    for (uint16_t goodId = 0; goodId < goods::GOOD_COUNT; ++goodId) {
        const SaleabilityInputs in = saleabilityInputsFor(gameState, market, view, player, goodId);
        const int32_t score        = saleability(in);
        if (dumpScore && (in.held > 0 || in.isIncumbent)) {
            std::fprintf(stderr,
                         "[moneyscore] t=%d P%u good=%u score=%d held=%d acc=%d/%d draw=%d "
                         "swing=%d price=%d dur=%d base=%d%s\n",
                         gameState.currentTurn(), static_cast<unsigned>(player),
                         static_cast<unsigned>(goodId), score, in.held, in.acceptingWeight,
                         in.totalWeight, in.industrialDraw, in.priceSwing, in.price,
                         in.durability, in.basePrice, in.isIncumbent ? " incumbent" : "");
        }
        if (goodId == state.moneyGood) {
            incumbentScore = score;
        }
        if (score > bestScore) {
            bestScore = score;
            bestGood  = goodId;
        }
    }

    if (bestGood == NO_MONEY_GOOD || bestScore < MONEY_MINIMUM_SALEABILITY) {
        return;
    }
    if (state.moneyGood != NO_MONEY_GOOD) {
        if (bestGood == state.moneyGood) {
            return;
        }
        const int32_t needed =
            incumbentScore + (incumbentScore * MONEY_CHALLENGER_MARGIN_PCT) / 100;
        if (bestScore <= needed) {
            return; // not clearly better; the convention holds
        }
    }
    static_cast<void>(requestSetMoneyGood(gameState, player, static_cast<uint8_t>(bestGood)));
}

/// Systems with a central bank able to set a policy rate. Commodity coinage
/// has no such institution, and Barter has no money to price.
[[nodiscard]] bool hasCentralBank(MonetarySystemType s) {
    return s == MonetarySystemType::GoldStandard || s == MonetarySystemType::FiatMoney ||
           s == MonetarySystemType::Digital;
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
    if (p == nullptr) {
        return ErrorCode::EntityNotFound;
    }
    if (ratio <= 0.0f) {
        return ErrorCode::InvalidArgument;
    }
    // Debasement is a COINAGE act. There is no metal content to dilute once a
    // civ is on paper, and the fiat equivalent is printing.
    if (p->monetary().system != MonetarySystemType::CommodityMoney) {
        return ErrorCode::InvalidState;
    }
    return debaseCurrency(p->monetary(), ratio);
}

ErrorCode requestRemintCurrency(aoc::game::GameState& gameState, PlayerId player) {
    aoc::game::Player* p = actor(gameState, player);
    if (p == nullptr) {
        return ErrorCode::EntityNotFound;
    }
    if (p->monetary().debasement.debasementRatio <= 0.0f) {
        return ErrorCode::InvalidState; // nothing to put right
    }
    return remintCurrency(*p);
}

ErrorCode requestDevalueCurrency(aoc::game::GameState& gameState, PlayerId player,
                                 GlobalCurrencyWarState& warState) {
    aoc::game::Player* p = actor(gameState, player);
    if (p == nullptr) {
        return ErrorCode::EntityNotFound;
    }
    if (!isFiatClass(p->monetary().system)) {
        return ErrorCode::InvalidState; // a metal currency cannot be talked down
    }
    return devalueCurrency(gameState, p->monetary(), p->currencyDevaluation(), warState);
}

ErrorCode requestPrintMoney(aoc::game::GameState& gameState, PlayerId player,
                            CurrencyAmount amount) {
    aoc::game::Player* p = actor(gameState, player);
    if (p == nullptr) {
        return ErrorCode::EntityNotFound;
    }
    if (amount <= 0) {
        return ErrorCode::InvalidArgument;
    }
    if (!isFiatClass(p->monetary().system)) {
        return ErrorCode::InvalidState;
    }
    // printMoney caps at a share of GDP and returns what it actually issued;
    // zero means the cap refused the whole request.
    const CurrencyAmount issued = p->monetary().printMoney(amount);
    if (issued <= 0) {
        return ErrorCode::InvalidState;
    }
    p->addGold(issued, aoc::sim::MoneyFlow::printed());
    return ErrorCode::Ok;
}

ErrorCode requestSetInterestRate(aoc::game::GameState& gameState, PlayerId player, float rate) {
    aoc::game::Player* p = actor(gameState, player);
    if (p == nullptr) {
        return ErrorCode::EntityNotFound;
    }
    if (!(rate >= 0.0f) || rate > 1.0f) {
        // Rejects NaN as well: a NaN rate would poison velocity, every bond
        // yield and the exchange rate, and would not be visible as a zero.
        return ErrorCode::InvalidArgument;
    }
    if (!hasCentralBank(p->monetary().system)) {
        return ErrorCode::InvalidState;
    }
    setInterestRate(p->monetary(), rate);
    return ErrorCode::Ok;
}

float applyCentralBankPolicy(aoc::game::GameState& gameState, PlayerId player) {
    aoc::game::Player* p = actor(gameState, player);
    if (p == nullptr) {
        return 0.0f;
    }

    MonetaryStateComponent& state = p->monetary();
    if (!hasCentralBank(state.system)) {
        return state.interestRate;
    }

    // A hyperinflation crisis is already handled, and at maximum: the AI's
    // crisis response slams the rate to 0.25 and zeroes spending. Do not talk
    // it back down mid-emergency.
    if (p->currencyCrisis().activeCrisis == CrisisType::Hyperinflation) {
        return state.interestRate;
    }

    // Lean against inflation...
    const float gap = state.inflationRate - INFLATION_TARGET;
    float target    = NEUTRAL_RATE + gap * INFLATION_RESPONSE;

    // ...but not past what the debt can carry. Every point of rate costs
    // governmentDebt * rate per turn, so the more a civ owes relative to what
    // it produces, the less tightening it can afford.
    if (state.gdp > 0 && state.governmentDebt > 0) {
        const float debtToGdp =
            static_cast<float>(state.governmentDebt) / static_cast<float>(state.gdp);
        target -= debtToGdp * DEBT_RELIEF;
    }

    // A bubble inflating is a reason not to be the cheapest money in the room.
    const bool bubbleRisk = p->bubble().growthStreak >= 5;
    if (bubbleRisk && target < BUBBLE_FLOOR) {
        target = BUBBLE_FLOOR;
    }

    const float step    = std::clamp(target - state.interestRate, -MAX_STEP, MAX_STEP);
    const float newRate = state.interestRate + step;
    setInterestRate(state, newRate);
    return state.interestRate;
}

MoneyWorldView moneyWorldView(const aoc::game::GameState& gameState,
                              const DiplomacyManager* diplomacy, PlayerId player) {
    MoneyWorldView view;
    const aoc::game::Player* me =
        player < aoc::sim::CITY_STATE_PLAYER_BASE ? gameState.player(player) : nullptr;
    if (me == nullptr) {
        return view;
    }

    // What this civ's industry would eat. A recipe counts only if the civ can
    // actually run it: the tech is in and some city has the building. That is
    // the whole exit-to-paper mechanism in miniature -- a metal nothing can
    // consume scores high, and researching the recipe that consumes it is what
    // later makes it a poor money.
    for (const ProductionRecipe& recipe : allRecipes()) {
        if (recipe.isRecycling) {
            continue; // melting money back is not an industrial use of it
        }
        if (recipe.requiredTech.isValid() && !me->hasResearched(recipe.requiredTech)) {
            continue;
        }
        bool canBuild = false;
        for (const std::unique_ptr<aoc::game::City>& city : me->cities()) {
            if (city != nullptr && city->owner() == player &&
                city->hasBuilding(recipe.requiredBuilding)) {
                canBuild = true;
                break;
            }
        }
        if (!canBuild) {
            continue;
        }
        for (const RecipeInput& input : recipe.inputs) {
            if (input.consumed && input.goodId < goods::GOOD_COUNT) {
                view.industrialDraw[input.goodId] += std::max(0, input.amount);
            }
        }
    }

    // Who takes what. Weight 1 per met civ plus its live routes with us, so
    // contact counts and settlement counts for more.
    for (const std::unique_ptr<aoc::game::Player>& other : gameState.players()) {
        if (other == nullptr || other->id() == player ||
            other->id() >= aoc::sim::CITY_STATE_PLAYER_BASE) {
            continue;
        }
        if (diplomacy != nullptr && !diplomacy->relation(player, other->id()).hasMet) {
            continue;
        }
        const int32_t weight = 1 + routesBetween(gameState, player, other->id());
        view.totalWeight += weight;
        const uint8_t theirs = other->monetary().moneyGood;
        if (theirs != NO_MONEY_GOOD && theirs < goods::GOOD_COUNT) {
            view.acceptingWeight[theirs] += weight;
        }
    }
    return view;
}

SaleabilityInputs saleabilityInputsFor(const aoc::game::GameState& gameState, const Market& market,
                                       const MoneyWorldView& view, PlayerId player,
                                       uint16_t goodId) {
    SaleabilityInputs in;
    if (goodId >= goods::GOOD_COUNT) {
        return in;
    }
    in.held            = civHeldUnits(gameState, player, goodId);
    in.acceptingWeight = view.acceptingWeight[goodId];
    in.totalWeight     = view.totalWeight;
    in.industrialDraw  = view.industrialDraw[goodId];
    in.price           = std::max(1, market.price(goodId));
    in.durability      = moneyDurability(goodId);
    in.basePrice       = goodDef(goodId).basePrice;
    const aoc::game::Player* me = actorConst(gameState, player);
    in.isIncumbent              = me != nullptr && me->monetary().moneyGood == goodId;

    // Swing over the rolling window. Untouched history slots sit at zero, and
    // counting those would read as a collapse from the base price rather than
    // the calm of a good nobody has traded yet.
    const Market::GoodMarketData& data = market.marketData(goodId);
    int32_t lo                         = 0;
    int32_t hi                         = 0;
    bool seen                          = false;
    for (const int32_t p : data.priceHistory) {
        if (p <= 0) {
            continue;
        }
        if (!seen) {
            lo   = p;
            hi   = p;
            seen = true;
        } else {
            lo = std::min(lo, p);
            hi = std::max(hi, p);
        }
    }
    in.priceSwing = seen ? hi - lo : 0;
    return in;
}

/// Value density saturates at gold's: 40 + 3 x 25. The goods table prices
/// late-era metals (titanium 350, rare earth 420) for the economy they enter,
/// not for how much value a unit carries in the hand, and no natural good is
/// more portable as money than gold. Above the cap a good can tie gold on this
/// term, never beat it; the older convention then wins the tie on id order.
constexpr int32_t MONEY_DENSITY_CAP = 115;

int32_t saleability(const SaleabilityInputs& inputs) {
    // Holding the good is a gate for a candidate, not for the incumbent: a
    // money whose stock has all been coined is still this civ's money, and
    // making it score zero would hand the title to the first challenger above
    // the minimum the moment the dwell ran out.
    if (inputs.held <= 0 && !inputs.isIncumbent) {
        return 0; // a civ cannot monetise what it does not hold
    }
    // Holding the good is a gate first and only a nudge after: a civ with
    // twenty units and one with a thousand price in it alike, so the term spans
    // 80 to 100. Letting abundance dominate is precisely preferredCoinTier's
    // failing: it reads reserves alone, so every civ on every seed measured
    // landed on copper, which is merely what gets mined most. A 40-point spread
    // was still enough for iron ore, mined two a turn from the first mine, to
    // beat gold ore held five at a time on both seeds; at 20 gold's density
    // carries it once a civ holds a handful. Acceptance has to be able to beat
    // abundance, or nothing ever converges on anything but the commonest ore.
    const int32_t stock = 80 + std::clamp(inputs.held, 0, 20);

    // The network term. A good is money because others take it, so acceptance
    // compounds and civs converge. With no contact at all it is neutral rather
    // than zero, or a civ that has met nobody could never adopt anything.
    const int32_t acceptance =
        inputs.totalWeight > 0
            ? 100 +
                  (150 * std::min(inputs.acceptingWeight, inputs.totalWeight)) / inputs.totalWeight
            : 100;

    // What industry eats, the people cannot hoard. Early this is near 100 for a
    // metal nothing consumes; when industrial uses arrive it collapses, and that
    // is what makes paper worth adopting rather than a scripted event.
    const int32_t industrial =
        std::max(0, 100 - (100 * std::max(0, inputs.industrialDraw)) / std::max(1, inputs.held));

    // Stable value is the whole point of holding money rather than goods. The
    // swing is read against the good's base price, not its current one, and
    // floored high: a metal a people hoards has a thin market, a thin market's
    // price sits at the floor and jumps on every sale, and measured against
    // that price silver ore held ten deep scored 19 while a single pearl scored
    // 27. Only a swing past half the good's own value counts as instability,
    // and instability alone never disqualifies.
    const int32_t stability =
        std::clamp(100 - (100 * std::max(0, inputs.priceSwing)) / (2 * std::max(1, inputs.basePrice)),
                   60, 100);

    // The physical half of the rule. Before these two terms the score ranked
    // wheat, stone, coffee and barite as money on both measured seeds and never
    // once a metal: nothing told it that grain rots and stone is heavy.
    const int32_t durability = std::clamp(inputs.durability, 0, 100);
    const int32_t density    = std::min(MONEY_DENSITY_CAP, 40 + 3 * std::max(0, inputs.basePrice));

    const int64_t score = static_cast<int64_t>(stock) * acceptance * industrial * stability *
                          durability * density;
    return static_cast<int32_t>(score / (100LL * 100 * 100 * 100 * 100));
}

} // namespace aoc::sim
