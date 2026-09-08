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

} // namespace aoc::sim
