/**
 * @file EconomicDepth.cpp
 * @brief Futures trading, labor strikes, insurance, economic espionage, migration.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/economy/EconomicDepth.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/Happiness.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/city/CityLoyalty.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/monetary/CurrencyTrust.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim {

// ============================================================================
// Futures Trading
// ============================================================================

ErrorCode buyFuture(aoc::game::GameState& gameState, const Market& market,
                     PlayerId buyer, uint16_t goodId, int32_t amount) {
    if (amount <= 0 || goodId >= market.goodsCount()) {
        return ErrorCode::InvalidArgument;
    }

    int32_t price = market.price(goodId);
    CurrencyAmount totalCost = static_cast<CurrencyAmount>(price * amount);

    aoc::game::Player* buyerPlayer = gameState.player(buyer);
    if (buyerPlayer == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    MonetaryStateComponent& monetary = buyerPlayer->monetary();
    if (monetary.treasury < totalCost) {
        return ErrorCode::InsufficientResources;
    }
    monetary.treasury -= totalCost;

    FuturesContract contract{};
    contract.buyer   = buyer;
    contract.seller  = INVALID_PLAYER;
    contract.goodId  = goodId;
    contract.amount  = amount;
    contract.contractPrice     = price;
    contract.turnsToSettlement = 5;
    buyerPlayer->futures().contracts.push_back(contract);

    LOG_INFO("Player %u bought future: %d x good %u at price %d",
             static_cast<unsigned>(buyer), amount, static_cast<unsigned>(goodId), price);
    return ErrorCode::Ok;
}

ErrorCode sellFuture(aoc::game::GameState& gameState, const Market& market,
                      PlayerId seller, uint16_t goodId, int32_t amount) {
    if (amount <= 0 || goodId >= market.goodsCount()) {
        return ErrorCode::InvalidArgument;
    }

    int32_t price    = market.price(goodId);
    CurrencyAmount revenue = static_cast<CurrencyAmount>(price * amount);

    aoc::game::Player* sellerPlayer = gameState.player(seller);
    if (sellerPlayer == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    sellerPlayer->monetary().treasury += revenue;

    FuturesContract contract{};
    contract.buyer             = INVALID_PLAYER;
    contract.seller            = seller;
    contract.goodId            = goodId;
    contract.amount            = amount;
    contract.contractPrice     = price;
    contract.turnsToSettlement = 5;
    sellerPlayer->futures().contracts.push_back(contract);

    return ErrorCode::Ok;
}

void settleFutures(aoc::game::GameState& gameState, Market& market) {
    // Cash-settled futures. Buyer paid contractPrice*amount up front as margin;
    // at settlement they receive currentPrice*amount back (net P/L = priceDiff
    // * amount). Seller received contractPrice*amount up front and must now
    // pay currentPrice*amount (net P/L = -priceDiff * amount). This keeps
    // accounting symmetric and avoids free-gold exploits from the old path
    // that only credited buyers.
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }
        std::vector<FuturesContract>& contracts = playerPtr->futures().contracts;
        std::vector<FuturesContract>::iterator it = contracts.begin();
        while (it != contracts.end()) {
            --it->turnsToSettlement;
            if (it->turnsToSettlement <= 0) {
                const int32_t        currentPrice = market.price(it->goodId);
                const CurrencyAmount mtm          =
                    static_cast<CurrencyAmount>(currentPrice) * it->amount;

                if (it->buyer != INVALID_PLAYER) {
                    aoc::game::Player* buyerPlayer = gameState.player(it->buyer);
                    if (buyerPlayer != nullptr) {
                        buyerPlayer->monetary().treasury += mtm;
                    }
                }
                if (it->seller != INVALID_PLAYER) {
                    aoc::game::Player* sellerPlayer = gameState.player(it->seller);
                    if (sellerPlayer != nullptr) {
                        sellerPlayer->monetary().treasury -= mtm;
                    }
                }

                it = contracts.erase(it);
            } else {
                ++it;
            }
        }
    }
}

// ============================================================================
// Labor Strikes
// ============================================================================

void checkLaborStrikes(aoc::game::GameState& gameState) {
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }

        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            if (cityPtr == nullptr) { continue; }

            const CityHappinessComponent& happiness = cityPtr->happiness();
            float netAmenities = happiness.amenities - happiness.demand;
            if (netAmenities >= 0.0f) { continue; }  // Happy city, no strike risk

            int32_t industrialBuildings = 0;
            for (const CityDistrictsComponent::PlacedDistrict& d : cityPtr->districts().districts) {
                if (d.type == DistrictType::Industrial) {
                    industrialBuildings += static_cast<int32_t>(d.buildings.size());
                }
            }

            if (industrialBuildings < 3) { continue; }

            CityStrikeComponent& strike = cityPtr->strike();
            if (!strike.isOnStrike) {
                strike.isOnStrike             = true;
                strike.strikeTurnsRemaining   = 3;
                LOG_INFO("LABOR STRIKE in city %s! Industrial buildings shut down for 3 turns",
                         cityPtr->name().c_str());
            }
        }
    }
}

void processStrikes(aoc::game::GameState& gameState) {
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }
        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            if (cityPtr == nullptr) { continue; }
            CityStrikeComponent& strike = cityPtr->strike();
            if (!strike.isOnStrike) { continue; }
            --strike.strikeTurnsRemaining;
            if (strike.strikeTurnsRemaining <= 0) {
                strike.isOnStrike = false;
                LOG_INFO("Strike ended in city %s", cityPtr->name().c_str());
            }
        }
    }
}

// ============================================================================
// Insurance
// ============================================================================

void processInsurancePremiums(aoc::game::GameState& gameState) {
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }

        const PlayerInsuranceComponent& ins = playerPtr->insurance();
        int32_t premium = ins.totalPremium();
        if (premium <= 0) { continue; }

        playerPtr->monetary().treasury -= static_cast<CurrencyAmount>(premium);
    }
}

// ============================================================================
// Migration
// ============================================================================

void processMigration(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid) {
    // Collect all cities with their owner and entity index for migration hashing.
    struct CityRecord {
        aoc::game::City*  cityPtr;
        PlayerId          owner;
        uint32_t          entityIndex;  // Used for deterministic migration hash
    };

    std::vector<CityRecord> allCities;
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }
        PlayerId ownerId = playerPtr->id();
        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            if (cityPtr == nullptr) { continue; }
            // Use a stable hash based on city location coordinates
            uint32_t hashIdx = static_cast<uint32_t>(cityPtr->location().q * 1000
                                                    + cityPtr->location().r);
            allCities.push_back({cityPtr.get(), ownerId, hashIdx});
        }
    }

    for (std::size_t a = 0; a < allCities.size(); ++a) {
        aoc::game::City* cityA = allCities[a].cityPtr;
        float qolA = cityA->happiness().amenities - cityA->happiness().demand;

        for (std::size_t b = a + 1; b < allCities.size(); ++b) {
            aoc::game::City* cityB = allCities[b].cityPtr;

            if (allCities[b].owner == allCities[a].owner) {
                continue;  // Same player, no migration
            }

            int32_t dist = grid.distance(cityA->location(), cityB->location());
            if (dist > 10) { continue; }

            float qolB    = cityB->happiness().amenities - cityB->happiness().demand;
            float qolDiff = qolB - qolA;
            if (std::abs(qolDiff) < 3.0f) { continue; }

            // Determine source and destination
            aoc::game::City* source = nullptr;
            aoc::game::City* dest   = nullptr;
            PlayerId destOwner      = INVALID_PLAYER;
            uint32_t sourceHash     = 0;

            if (qolDiff > 0.0f) {
                source     = cityA;
                dest       = cityB;
                destOwner  = allCities[b].owner;
                sourceHash = allCities[a].entityIndex;
            } else {
                source     = cityB;
                dest       = cityA;
                destOwner  = allCities[a].owner;
                sourceHash = allCities[b].entityIndex;
            }

            // Check destination's immigration policy
            aoc::game::Player* destPlayer = gameState.player(destOwner);
            if (destPlayer == nullptr) { continue; }
            ImmigrationPolicy destPolicy = destPlayer->migration().policy;
            if (destPolicy == ImmigrationPolicy::Closed) { continue; }

            // Migration happens slowly: ~1 citizen per 10 qualifying turns.
            // Hash must vary with turn number — prior formula was pure in the
            // tile coordinate and either always fired or never fired for a
            // given pair for the full game. Mixing turnNumber produces a
            // deterministic but pair-and-time-dependent gate.
            uint32_t migHash = (sourceHash
                              ^ static_cast<uint32_t>(gameState.currentTurn()))
                             * 2654435761u;
            if ((migHash % 10) != 0) { continue; }

            if (source->population() > 2) {
                source->setPopulation(source->population() - 1);
                dest->setPopulation(dest->population() + 1);
                LOG_INFO("Migration: 1 citizen moved from %s to %s (QoL difference %.1f)",
                         source->name().c_str(), dest->name().c_str(),
                         static_cast<double>(std::abs(qolDiff)));
            }
        }
    }
    (void)grid;  // Grid reserved for future distance/road weighting
}

} // namespace aoc::sim
