/**
 * @file MoneyFlow.cpp
 * @brief The money ledger and the world-money sum.
 */

#include "aoc/simulation/monetary/MoneyFlow.hpp"

#include "aoc/core/Log.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/citystate/CityState.hpp"
#include "aoc/simulation/monetary/ForexMarket.hpp"
#include "aoc/simulation/monetary/CurrencyTrust.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"

#include <algorithm>

namespace aoc::sim {

void MoneyLedger::reset() {
    this->civs.fill(Civ{});
}

void MoneyLedger::record(PlayerId who, MoneyFlow flow, CurrencyAmount delta) {
    if (who >= MAX_PLAYERS || delta == 0) {
        return;
    }
    Civ& civ = this->civs[static_cast<std::size_t>(who)];
    switch (flow.kind) {
    case MoneyFlowKind::Domestic:
    case MoneyFlowKind::Transfer:
        break; // moves inside the world
    case MoneyFlowKind::External:
        (delta > 0 ? civ.externalIn : civ.externalOut) += (delta > 0 ? delta : -delta);
        break;
    case MoneyFlowKind::Monetised:
    case MoneyFlowKind::Demonetised:
        // Money made from a good, or a good made from money: it enters or
        // leaves the world like an external flow, and is also kept readable.
        (delta > 0 ? civ.externalIn : civ.externalOut) += (delta > 0 ? delta : -delta);
        civ.monetised += delta;
        break;
    case MoneyFlowKind::Printed:
        civ.printed += delta;
        break;
    case MoneyFlowKind::Loss:
        civ.lost += (delta > 0 ? delta : -delta);
        break;
    case MoneyFlowKind::Unbacked:
        (delta > 0 ? civ.unbackedIn : civ.unbackedOut) += (delta > 0 ? delta : -delta);
        break;
    }
}

MoneyLedger::Civ MoneyLedger::total() const {
    Civ sum;
    for (const Civ& civ : this->civs) {
        sum.printed += civ.printed;
        sum.externalIn += civ.externalIn;
        sum.externalOut += civ.externalOut;
        sum.lost += civ.lost;
        sum.unbackedIn += civ.unbackedIn;
        sum.unbackedOut += civ.unbackedOut;
        sum.monetised += civ.monetised;
    }
    return sum;
}

int64_t MoneyLedger::expectedDelta() const {
    const Civ t = this->total();
    return t.printed + t.externalIn - t.externalOut - t.lost + t.unbackedIn - t.unbackedOut;
}

bool MoneyLedger::backed() const {
    const Civ t = this->total();
    return t.unbackedIn == 0 && t.unbackedOut == 0;
}

namespace {

/// Coin, and under a paper regime the notes too: what a tax can reach.
[[nodiscard]] CurrencyAmount privateMoneyOf(const aoc::game::Player& civ) {
    const MonetaryStateComponent& m = civ.monetary();
    return std::max<CurrencyAmount>(0, m.privateSpecie) +
           (notesInUse(m.system) ? std::max<CurrencyAmount>(0, m.privateNotes) : 0);
}

/// City-state seats and the barbarian seat hold money the world does not
/// count (worldMoney walks players() only): any flow across this line is
/// external for the civ inside it.
/// Draw money out of a civ's private pools the way a domestic tax does: notes
/// first under a paper regime, then coin, and never past either pool. Callers
/// cap `amount` at privateMoneyOf first; taking it all from specie instead let
/// a fiat civ's notes stand while its coin went negative.
void drawPrivate(aoc::game::Player& people, CurrencyAmount amount) {
    MonetaryStateComponent& m = people.monetary();
    CurrencyAmount left       = amount;
    if (notesInUse(m.system)) {
        const CurrencyAmount notes = std::min(left, std::max<CurrencyAmount>(0, m.privateNotes));
        m.privateNotes -= notes;
        left -= notes;
    }
    const CurrencyAmount coin = std::min(left, std::max<CurrencyAmount>(0, m.privateSpecie));
    m.privateSpecie -= coin;
}

[[nodiscard]] bool outsideWorld(PlayerId id) {
    return id != INVALID_PLAYER && id >= CITY_STATE_PLAYER_BASE;
}

/// The other civ's people, when `civ` names a major civ other than `self`.
[[nodiscard]] aoc::game::Player* foreignPeople(aoc::game::GameState& gameState, PlayerId civ,
                                               const aoc::game::Player& self) {
    if (civ == self.id() || civ == INVALID_PLAYER || civ >= CITY_STATE_PLAYER_BASE) {
        return nullptr;
    }
    return gameState.player(civ);
}

} // namespace

bool moneyless(const aoc::game::Player& player) {
    return player.monetary().system == MonetarySystemType::Barter;
}

CurrencyAmount payFromTreasury(aoc::game::Player& payer, CurrencyAmount amount) {
    const CurrencyAmount paid = std::min(amount, std::max<CurrencyAmount>(0, payer.treasury()));
    if (paid > 0) {
        payer.addGold(-paid, MoneyFlow::domestic(payer.id()));
    }
    return paid;
}

CurrencyAmount payFromTreasury(aoc::game::GameState& gameState, aoc::game::Player& payer,
                               CurrencyAmount amount, PlayerId civ) {
    if (outsideWorld(payer.id())) {
        // A city-state's coin arriving in a major civ's province comes from
        // beyond the ledger.
        aoc::game::Player* locals = outsideWorld(civ) ? nullptr : gameState.player(civ);
        const CurrencyAmount paid = std::min(amount, std::max<CurrencyAmount>(0, payer.treasury()));
        if (paid > 0) {
            payer.addGold(-paid, MoneyFlow::external());
            if (locals != nullptr) {
                locals->monetary().privateSpecie += paid;
                bookExternal(*locals, paid);
            }
        }
        return paid;
    }
    if (outsideWorld(civ)) {
        const CurrencyAmount paid = std::min(amount, std::max<CurrencyAmount>(0, payer.treasury()));
        if (paid > 0) {
            payer.addGold(-paid,
                          MoneyFlow::external()); // a city-state's people are the external sector
        }
        return paid;
    }
    aoc::game::Player* locals = foreignPeople(gameState, civ, payer);
    if (locals == nullptr) {
        return payFromTreasury(payer, amount);
    }
    const CurrencyAmount paid = std::min(amount, std::max<CurrencyAmount>(0, payer.treasury()));
    if (paid > 0) {
        payer.addGold(-paid, MoneyFlow::domestic(civ));
        locals->monetary().privateSpecie += paid;
    }
    return paid;
}

CurrencyAmount takeFromPrivate(aoc::game::Player& taker, CurrencyAmount amount) {
    const CurrencyAmount taken = std::min(amount, privateMoneyOf(taker));
    if (taken > 0) {
        taker.addGold(taken, MoneyFlow::domestic(taker.id()));
    }
    return taken;
}

CurrencyAmount takeFromPrivate(aoc::game::GameState& gameState, PlayerId civ,
                               aoc::game::Player& taker, CurrencyAmount amount) {
    if (amount <= 0) {
        return 0;
    }
    aoc::game::Player* people = foreignPeople(gameState, civ, taker);
    if (people == nullptr) {
        if (civ != INVALID_PLAYER && civ != taker.id()) {
            return 0; // a city-state's or nobody's people owe this treasury nothing
        }
        return takeFromPrivate(taker, amount);
    }
    const CurrencyAmount taken = std::min(amount, privateMoneyOf(*people));
    if (taken > 0) {
        drawPrivate(*people, taken);
        if (outsideWorld(taker.id())) {
            bookExternal(*people, -taken); // a barbarian's or city-state's hoard: out of the world
        }
        taker.addGold(taken, MoneyFlow::domestic(civ));
    }
    return taken;
}

CurrencyAmount plunder(aoc::game::GameState& gameState, PlayerId victim, aoc::game::Player& captor,
                       CurrencyAmount amount) {
    if (amount <= 0) {
        return 0;
    }
    aoc::game::Player* loser = (victim == captor.id() || victim == BARBARIAN_PLAYER ||
                                (victim != INVALID_PLAYER && victim >= CITY_STATE_PLAYER_BASE))
                                   ? nullptr
                                   : gameState.player(victim);
    if (loser == nullptr) {
        captor.addGold(amount, MoneyFlow::external());
        return amount;
    }
    CurrencyAmount taken = takeFromPrivate(gameState, victim, captor, amount);
    const CurrencyAmount fromTreasury =
        std::min(amount - taken, std::max<CurrencyAmount>(0, loser->treasury()));
    if (fromTreasury > 0) {
        loser->addGold(-fromTreasury, MoneyFlow::transfer(captor.id()));
        captor.addGold(fromTreasury, MoneyFlow::transfer(victim));
        taken += fromTreasury;
    }
    return taken;
}

namespace {

/// Up to `units` of `goodId` out of `city`, returning what came out.
int64_t takeUpTo(aoc::game::City& city, uint16_t goodId, int64_t units) {
    const int64_t have  = std::max<int64_t>(0, city.stockpile().getAmount(goodId));
    const int64_t taken = std::min(units, have);
    if (taken > 0) {
        city.stockpile().consumeGoods(goodId, static_cast<int32_t>(taken));
    }
    return taken;
}

} // namespace

CurrencyAmount coinToPay(aoc::game::Player& buyer, aoc::game::City& at, CurrencyAmount need,
                         int32_t industrialDraw) {
    if (outsideWorld(buyer.id())) {
        return 0;
    }
    MonetaryStateComponent& m = buyer.monetary();
    if (isFiatClass(m.system) || m.moneyGood >= goods::GOOD_COUNT) {
        return 0;
    }
    const int64_t specie = std::max<CurrencyAmount>(0, m.privateSpecie);
    if (need <= specie) {
        return 0;
    }
    const uint16_t good = m.moneyGood;
    const int64_t par   = std::max<int64_t>(1, goodDef(good).basePrice);
    int64_t stock       = 0;
    for (const std::unique_ptr<aoc::game::City>& city : buyer.cities()) {
        if (city != nullptr && city->owner() == buyer.id()) {
            stock += std::max<int64_t>(0, city->stockpile().getAmount(good));
        }
    }
    const int64_t wanted = (need - specie + par - 1) / par;
    int64_t units        = std::min(wanted, stock - std::max<int64_t>(0, industrialDraw));
    if (units <= 0) {
        return 0;
    }
    int64_t taken = at.owner() == buyer.id() ? takeUpTo(at, good, units) : 0;
    for (const std::unique_ptr<aoc::game::City>& city : buyer.cities()) {
        if (taken >= units) {
            break;
        }
        if (city != nullptr && city.get() != &at && city->owner() == buyer.id()) {
            taken += takeUpTo(*city, good, units - taken);
        }
    }
    const CurrencyAmount value = static_cast<CurrencyAmount>(taken * par);
    m.privateSpecie += value;
    if (buyer.moneyLedger() != nullptr) {
        buyer.moneyLedger()->record(buyer.id(), MoneyFlow::monetised(), value);
    }
    LOG_INFO("Player %u coined %lld %.*s at par %lld to pay", static_cast<unsigned>(buyer.id()),
             static_cast<long long>(taken), static_cast<int>(goodDef(good).name.size()),
             goodDef(good).name.data(), static_cast<long long>(par));
    return value;
}

CurrencyAmount payInSpecie(aoc::game::Player& buyer, CurrencyAmount price) {
    const CurrencyAmount paid =
        std::min(price, std::max<CurrencyAmount>(0, buyer.monetary().privateSpecie));
    if (paid > 0) {
        buyer.monetary().privateSpecie -= paid;
    }
    return paid;
}

CurrencyAmount payInNotes(aoc::game::Player& buyer, CurrencyAmount price) {
    const CurrencyAmount paid =
        std::min(price, std::max<CurrencyAmount>(0, buyer.monetary().privateNotes));
    if (paid > 0) {
        buyer.monetary().privateNotes -= paid;
    }
    return paid;
}

bool settlesInNotes(const aoc::game::Player& seller, const aoc::game::Player& buyer) {
    if (!notesInUse(seller.monetary().system) || !notesInUse(buyer.monetary().system)) {
        return false;
    }
    return buyer.currencyTrust().trustScore >= 0.5f || seller.currencyTrust().isReserveCurrency;
}

float settlementRate(const aoc::game::Player& seller, const aoc::game::Player& buyer) {
    const float sellerRate = std::max(0.01f, seller.currencyExchange().exchangeRate);
    const float buyerRate  = std::max(0.01f, buyer.currencyExchange().exchangeRate);
    return std::clamp(sellerRate / buyerRate, 0.5f, 2.0f);
}

CurrencyAmount receiveTradeCoin(aoc::game::Player& seller, CurrencyAmount amount, bool notes) {
    if (amount <= 0) {
        return 0;
    }
    if (moneyless(seller)) {
        seller.monetary().bullion += amount; // paper never reaches a Barter seller
        return 0;
    }
    (notes ? seller.monetary().privateNotes : seller.monetary().privateSpecie) +=
        amount; // the merchants' proceeds
    const CurrencyAmount share =
        static_cast<CurrencyAmount>(static_cast<float>(amount) * seller.monetary().taxRate);
    return takeFromPrivate(seller, share); // and the customs on them
}

void giveToPrivate(aoc::game::Player& civ, CurrencyAmount amount, bool notes) {
    if (amount > 0) {
        (notes && notesInUse(civ.monetary().system) ? civ.monetary().privateNotes
                                                    : civ.monetary().privateSpecie) += amount;
    }
}

void loseCoin(const aoc::game::Player& owner, CurrencyAmount coin) {
    if (coin > 0 && owner.moneyLedger() != nullptr) {
        owner.moneyLedger()->record(owner.id(), MoneyFlow::loss(), coin);
    }
}

void bookExternal(const aoc::game::Player& civ, CurrencyAmount delta) {
    if (delta != 0 && civ.moneyLedger() != nullptr) {
        civ.moneyLedger()->record(civ.id(), MoneyFlow::external(), delta);
    }
}

int64_t worldMoney(const aoc::game::GameState& gameState) {
    int64_t sum = 0;
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        if (player == nullptr) {
            continue;
        }
        const MonetaryStateComponent& m = player->monetary();
        sum += m.treasury + m.privateSpecie + m.privateNotes + m.bullion;
        for (const std::unique_ptr<aoc::game::Unit>& unit : player->units()) {
            if (unit != nullptr && unitTypeDef(unit->typeId()).unitClass == UnitClass::Trader) {
                sum += unit->trader().carriedGold;
            }
        }
    }
    return sum;
}

bool moneyConserved(int64_t worldMoneyBefore, int64_t worldMoneyAfter, const MoneyLedger& ledger) {
    return ledger.backed() && worldMoneyAfter - worldMoneyBefore == ledger.expectedDelta();
}

} // namespace aoc::sim
