/**
 * @file MoneyFlow.cpp
 * @brief The money ledger and the world-money sum.
 */

#include "aoc/simulation/monetary/MoneyFlow.hpp"

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"

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
