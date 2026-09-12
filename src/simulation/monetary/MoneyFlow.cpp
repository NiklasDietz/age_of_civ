/**
 * @file MoneyFlow.cpp
 * @brief The money ledger and the world-money sum.
 */

#include "aoc/simulation/monetary/MoneyFlow.hpp"

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/citystate/CityState.hpp"
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
        case MoneyFlowKind::Minted:
            civ.minted += delta;
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
        sum.minted += civ.minted;
        sum.printed += civ.printed;
        sum.externalIn += civ.externalIn;
        sum.externalOut += civ.externalOut;
        sum.lost += civ.lost;
        sum.unbackedIn += civ.unbackedIn;
        sum.unbackedOut += civ.unbackedOut;
    }
    return sum;
}

int64_t MoneyLedger::expectedDelta() const {
    const Civ t = this->total();
    return t.minted + t.printed + t.externalIn - t.externalOut - t.lost + t.unbackedIn - t.unbackedOut;
}

bool MoneyLedger::backed() const {
    const Civ t = this->total();
    return t.unbackedIn == 0 && t.unbackedOut == 0;
}

namespace {

[[nodiscard]] CurrencyAmount privateMoneyOf(const aoc::game::Player& civ) {
    return std::max<CurrencyAmount>(0, civ.monetary().privateSpecie);
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
    if (civ != INVALID_PLAYER && civ >= CITY_STATE_PLAYER_BASE) {
        const CurrencyAmount paid = std::min(amount, std::max<CurrencyAmount>(0, payer.treasury()));
        if (paid > 0) {
            payer.addGold(-paid, MoneyFlow::external()); // a city-state's people are the external sector
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
        people->monetary().privateSpecie -= taken;
        taker.addGold(taken, MoneyFlow::domestic(civ));
    }
    return taken;
}

CurrencyAmount plunder(aoc::game::GameState& gameState, PlayerId victim, aoc::game::Player& captor,
                       CurrencyAmount amount) {
    if (amount <= 0) {
        return 0;
    }
    aoc::game::Player* loser =
        (victim == captor.id() || victim == BARBARIAN_PLAYER) ? nullptr : gameState.player(victim);
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
