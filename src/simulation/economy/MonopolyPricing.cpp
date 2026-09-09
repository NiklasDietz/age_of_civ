/**
 * @file MonopolyPricing.cpp
 * @brief Resource monopoly detection and cartel pricing.
 */

#include "aoc/simulation/event/GameNotifications.hpp"

#include <algorithm>
#include <string>
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/citystate/CityState.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/economy/MonopolyPricing.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/core/Deterministic.hpp"
#include "aoc/core/Log.hpp"

#include <unordered_map>

namespace aoc::sim {

void detectMonopolies(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid) {
    GlobalMonopolyComponent& mono = gameState.monopoly();

    constexpr uint16_t TRACKED[] = {
        goods::IRON_ORE, goods::COPPER_ORE, goods::COAL, goods::OIL,
        goods::HORSES,   goods::NITER,      goods::URANIUM, goods::ALUMINUM,
        goods::RUBBER,   goods::TIN,        goods::SILVER_ORE, goods::GOLD_ORE
    };
    mono.trackedCount = 12;

    std::array<std::unordered_map<PlayerId, int32_t>, 12> playerSupply = {};
    std::array<int32_t, 12> totalSupply = {};

    // Count supply from tile resources
    for (int32_t tile = 0; tile < grid.tileCount(); ++tile) {
        ResourceId res = grid.resource(tile);
        if (!res.isValid()) { continue; }
        PlayerId tileOwner = grid.owner(tile);
        if (tileOwner == INVALID_PLAYER) { continue; }

        for (int32_t g = 0; g < 12; ++g) {
            if (res.value == TRACKED[g]) {
                int16_t reserves = grid.reserves(tile);
                int32_t value    = (reserves < 0) ? 10 : static_cast<int32_t>(reserves);
                playerSupply[static_cast<std::size_t>(g)][tileOwner] += value;
                totalSupply[static_cast<std::size_t>(g)]             += value;
                break;
            }
        }
    }

    // Count supply from city stockpiles
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }
        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            if (cityPtr == nullptr) { continue; }
            const CityStockpileComponent& stockpile = cityPtr->stockpile();
            for (int32_t g = 0; g < 12; ++g) {
                int32_t amount = stockpile.getAmount(TRACKED[g]);
                if (amount > 0) {
                    playerSupply[static_cast<std::size_t>(g)][playerPtr->id()] += amount;
                    totalSupply[static_cast<std::size_t>(g)]                   += amount;
                }
            }
        }
    }

    for (int32_t g = 0; g < 12; ++g) {
        std::size_t  idx  = static_cast<std::size_t>(g);
        MonopolyInfo& info = mono.monopolies[idx];
        info.goodId = TRACKED[g];

        if (totalSupply[idx] <= 0) {
            info.isActive      = false;
            info.monopolist    = INVALID_PLAYER;
            info.controlShare  = 0.0f;
            info.priceMultiplier = 1.0f;
            continue;
        }

        // playerSupply[idx] is an unordered_map; break supply ties by lowest
        // PlayerId so the monopolist pick is order-independent. (A tie at the
        // max forces share <= 0.5, below the 0.60 activation gate, so topPlayer
        // is never committed on a tie today -- this is defensive uniformity
        // that also hardens the site against a future lower gate.)
        const std::pair<PlayerId, int32_t> top =
            aoc::core::argMaxByValueLowestKey(playerSupply[idx], INVALID_PLAYER);
        const PlayerId topPlayer = top.first;
        const int32_t  topAmount = top.second;

        float share       = static_cast<float>(topAmount) / static_cast<float>(totalSupply[idx]);
        info.controlShare = share;
        bool wasActive    = info.isActive;

        if (share >= 0.60f) {
            info.isActive   = true;
            info.monopolist = topPlayer;
            // The share sets the CEILING, not the price. Charging is a choice.
            if (share >= 0.80f) {
                info.maxPriceMultiplier = 3.0f;
            } else if (share >= 0.70f) {
                info.maxPriceMultiplier = 2.0f;
            } else {
                info.maxPriceMultiplier = 1.5f;
            }
            // A markup already chosen cannot exceed a ceiling that has fallen.
            info.priceMultiplier = std::min(info.priceMultiplier, info.maxPriceMultiplier);

            if (!wasActive) {
                LOG_INFO("MONOPOLY: player %u controls %.0f%% of %.*s supply (may charge up to %.1fx)",
                         static_cast<unsigned>(topPlayer),
                         static_cast<double>(share) * 100.0,
                         static_cast<int>(goodDef(TRACKED[g]).name.size()),
                         goodDef(TRACKED[g]).name.data(),
                         static_cast<double>(info.maxPriceMultiplier));
                // Tell the holder. Realising you have a monopoly should not
                // depend on reading the market screen closely enough to spot it.
                aoc::sim::event::GameNotification note{};
                note.category = aoc::sim::event::NotificationCategory::Economy;
                note.title    = "Monopoly gained";
                note.body     = "You control " +
                                std::to_string(static_cast<int32_t>(share * 100.0f)) +
                                "% of the world's " +
                                std::string(goodDef(TRACKED[g]).name) +
                                ". You may charge buyers up to " +
                                std::to_string(static_cast<int32_t>(info.maxPriceMultiplier * 100.0f)) +
                                "% of the market price.";
                note.relevantPlayer = topPlayer;
                note.priority       = 2;
                aoc::sim::event::pushNotification(note);
            }
        } else {
            if (wasActive) {
                LOG_INFO("Monopoly broken: %.*s supply now distributed",
                         static_cast<int>(goodDef(TRACKED[g]).name.size()),
                         goodDef(TRACKED[g]).name.data());
            }
            info.isActive           = false;
            info.monopolist         = INVALID_PLAYER;
            info.priceMultiplier    = 1.0f;
            info.maxPriceMultiplier = 1.0f;
        }
    }
}

void applyMonopolyIncome(aoc::game::GameState& gameState) {
    const GlobalMonopolyComponent& mono = gameState.monopoly();

    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }
        CurrencyAmount income = mono.monopolyIncome(playerPtr->id());
        if (income > 0) {
            playerPtr->monetary().treasury += income;
        }
    }
}

ErrorCode requestSetMonopolyPrice(GlobalMonopolyComponent& monopolies, PlayerId player,
                                  uint16_t goodId, float multiplier) {
    for (int32_t i = 0; i < monopolies.trackedCount; ++i) {
        MonopolyInfo& info = monopolies.monopolies[i];
        if (info.goodId != goodId) {
            continue;
        }
        if (!info.isActive || info.monopolist != player) {
            return ErrorCode::InvalidArgument; // not yours to price
        }
        info.priceMultiplier = std::clamp(multiplier, 1.0f, info.maxPriceMultiplier);
        LOG_INFO("Player %u set its %.*s markup to %.2fx (ceiling %.2fx)",
                 static_cast<unsigned>(player),
                 static_cast<int>(goodDef(goodId).name.size()), goodDef(goodId).name.data(),
                 static_cast<double>(info.priceMultiplier),
                 static_cast<double>(info.maxPriceMultiplier));
        return ErrorCode::Ok;
    }
    return ErrorCode::InvalidArgument;
}

void aiChooseMonopolyPrices(aoc::game::GameState& gameState, PlayerId player,
                            const DiplomacyManager& diplomacy) {
    if (player >= CITY_STATE_PLAYER_BASE) { return; }

    GlobalMonopolyComponent& mono = gameState.monopoly();

    // Squeeze the civs that already resent you; spare the ones you might yet
    // win over. Greed is the share of met civs that are ALREADY hostile.
    //
    // The first version of this counted friends instead and charged
    // (1 - friendly/met), which reads a neutral as someone with nothing to
    // lose. Early on every relation is Neutral, so every monopolist went
    // straight to its ceiling against the whole world, and the grievances
    // soured diplomacy globally from the first monopoly onward. Measured at
    // turn 340 that cost seed 42 42% of its cities and both seeds about 40% of
    // GDP. A civ you have not yet fallen out with is an asset, not a target.
    int32_t met     = 0;
    int32_t hostile = 0;
    for (const std::unique_ptr<aoc::game::Player>& other : gameState.players()) {
        if (other == nullptr) { continue; }
        const PlayerId id = other->id();
        if (id == player || id >= CITY_STATE_PLAYER_BASE) { continue; }
        const PairwiseRelation& rel = diplomacy.relation(player, id);
        if (!rel.hasMet) { continue; }
        ++met;
        if (rel.isAtWar) { ++hostile; continue; }
        const DiplomaticStance stance = rel.stance();
        if (stance == DiplomaticStance::Hostile || stance == DiplomaticStance::Unfriendly) {
            ++hostile;
        }
    }

    // Nobody met yet: no buyers to anger and no trade to tax. Leave it alone.
    if (met == 0) { return; }

    const float greed = static_cast<float>(hostile) / static_cast<float>(met);

    for (int32_t i = 0; i < mono.trackedCount; ++i) {
        const MonopolyInfo& info = mono.monopolies[i];
        if (!info.isActive || info.monopolist != player) { continue; }

        const float target = 1.0f + (info.maxPriceMultiplier - 1.0f) * greed;
        // Below a fifth of a point the markup rounds away in the integer price
        // at the delivery site, so it would buy grievances for nothing.
        if (target < 1.2f) { continue; }
        if (requestSetMonopolyPrice(mono, player, info.goodId, target) != ErrorCode::Ok) {
            continue;
        }
    }
}

} // namespace aoc::sim
