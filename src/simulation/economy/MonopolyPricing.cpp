/**
 * @file MonopolyPricing.cpp
 * @brief Resource monopoly detection and cartel pricing.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/economy/MonopolyPricing.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/map/HexGrid.hpp"
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

        PlayerId topPlayer = INVALID_PLAYER;
        int32_t  topAmount = 0;
        for (const std::pair<const PlayerId, int32_t>& entry : playerSupply[idx]) {
            if (entry.second > topAmount) {
                topAmount = entry.second;
                topPlayer = entry.first;
            }
        }

        float share       = static_cast<float>(topAmount) / static_cast<float>(totalSupply[idx]);
        info.controlShare = share;
        bool wasActive    = info.isActive;

        if (share >= 0.60f) {
            info.isActive   = true;
            info.monopolist = topPlayer;
            if (share >= 0.80f) {
                info.priceMultiplier = 3.0f;
            } else if (share >= 0.70f) {
                info.priceMultiplier = 2.0f;
            } else {
                info.priceMultiplier = 1.5f;
            }

            if (!wasActive) {
                LOG_INFO("MONOPOLY: player %u controls %.0f%% of %.*s supply (price: %.1fx)",
                         static_cast<unsigned>(topPlayer),
                         static_cast<double>(share) * 100.0,
                         static_cast<int>(goodDef(TRACKED[g]).name.size()),
                         goodDef(TRACKED[g]).name.data(),
                         static_cast<double>(info.priceMultiplier));
            }
        } else {
            if (wasActive) {
                LOG_INFO("Monopoly broken: %.*s supply now distributed",
                         static_cast<int>(goodDef(TRACKED[g]).name.size()),
                         goodDef(TRACKED[g]).name.data());
            }
            info.isActive        = false;
            info.monopolist      = INVALID_PLAYER;
            info.priceMultiplier = 1.0f;
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

} // namespace aoc::sim
