/**
 * @file EconomicDepth.cpp
 * @brief Futures trading, labor strikes, insurance, economic espionage, migration.
 */

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
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim {

// ============================================================================
// Futures Trading
// ============================================================================

ErrorCode buyFuture(aoc::ecs::World& world, const Market& market,
                     PlayerId buyer, uint16_t goodId, int32_t amount) {
    if (amount <= 0 || goodId >= market.goodsCount()) {
        return ErrorCode::InvalidArgument;
    }

    int32_t price = market.price(goodId);
    CurrencyAmount totalCost = static_cast<CurrencyAmount>(price * amount);

    // Deduct cost from buyer
    aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();
    if (monetaryPool == nullptr) { return ErrorCode::InvalidArgument; }

    for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
        if (monetaryPool->data()[i].owner == buyer) {
            if (monetaryPool->data()[i].treasury < totalCost) {
                return ErrorCode::InsufficientResources;
            }
            monetaryPool->data()[i].treasury -= totalCost;
            break;
        }
    }

    // Create futures contract
    aoc::ecs::ComponentPool<PlayerFuturesComponent>* futuresPool =
        world.getPool<PlayerFuturesComponent>();
    if (futuresPool != nullptr) {
        for (uint32_t i = 0; i < futuresPool->size(); ++i) {
            if (futuresPool->data()[i].owner == buyer) {
                FuturesContract contract{};
                contract.buyer = buyer;
                contract.seller = INVALID_PLAYER;
                contract.goodId = goodId;
                contract.amount = amount;
                contract.contractPrice = price;
                contract.turnsToSettlement = 5;
                futuresPool->data()[i].contracts.push_back(contract);
                break;
            }
        }
    }

    LOG_INFO("Player %u bought future: %d x good %u at price %d",
             static_cast<unsigned>(buyer), amount, static_cast<unsigned>(goodId), price);
    return ErrorCode::Ok;
}

ErrorCode sellFuture(aoc::ecs::World& world, const Market& market,
                      PlayerId seller, uint16_t goodId, int32_t amount) {
    if (amount <= 0 || goodId >= market.goodsCount()) {
        return ErrorCode::InvalidArgument;
    }

    int32_t price = market.price(goodId);
    CurrencyAmount revenue = static_cast<CurrencyAmount>(price * amount);

    // Seller receives gold now, promises to deliver goods later
    aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();
    if (monetaryPool != nullptr) {
        for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
            if (monetaryPool->data()[i].owner == seller) {
                monetaryPool->data()[i].treasury += revenue;
                break;
            }
        }
    }

    aoc::ecs::ComponentPool<PlayerFuturesComponent>* futuresPool =
        world.getPool<PlayerFuturesComponent>();
    if (futuresPool != nullptr) {
        for (uint32_t i = 0; i < futuresPool->size(); ++i) {
            if (futuresPool->data()[i].owner == seller) {
                FuturesContract contract{};
                contract.buyer = INVALID_PLAYER;
                contract.seller = seller;
                contract.goodId = goodId;
                contract.amount = amount;
                contract.contractPrice = price;
                contract.turnsToSettlement = 5;
                futuresPool->data()[i].contracts.push_back(contract);
                break;
            }
        }
    }

    return ErrorCode::Ok;
}

void settleFutures(aoc::ecs::World& world, Market& market) {
    aoc::ecs::ComponentPool<PlayerFuturesComponent>* futuresPool =
        world.getPool<PlayerFuturesComponent>();
    if (futuresPool == nullptr) { return; }

    for (uint32_t p = 0; p < futuresPool->size(); ++p) {
        std::vector<FuturesContract>& contracts = futuresPool->data()[p].contracts;
        std::vector<FuturesContract>::iterator it = contracts.begin();
        while (it != contracts.end()) {
            --it->turnsToSettlement;
            if (it->turnsToSettlement <= 0) {
                // Settlement: buyer receives goods at contract price
                // If current market price > contract price: buyer profits
                // If current market price < contract price: buyer loses
                int32_t currentPrice = market.price(it->goodId);
                int32_t priceDiff = currentPrice - it->contractPrice;
                CurrencyAmount settlement = static_cast<CurrencyAmount>(
                    priceDiff * it->amount);

                // Apply profit/loss to buyer
                aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
                    world.getPool<MonetaryStateComponent>();
                if (monetaryPool != nullptr && it->buyer != INVALID_PLAYER) {
                    for (uint32_t m = 0; m < monetaryPool->size(); ++m) {
                        if (monetaryPool->data()[m].owner == it->buyer) {
                            monetaryPool->data()[m].treasury += settlement;
                            break;
                        }
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

void checkLaborStrikes(aoc::ecs::World& world) {
    const aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    if (cityPool == nullptr) { return; }

    for (uint32_t c = 0; c < cityPool->size(); ++c) {
        const CityComponent& city = cityPool->data()[c];
        EntityId cityEntity = cityPool->entities()[c];
        if (city.owner == BARBARIAN_PLAYER) { continue; }

        // Check amenities
        const CityHappinessComponent* happiness =
            world.tryGetComponent<CityHappinessComponent>(cityEntity);
        if (happiness == nullptr) { continue; }

        float netAmenities = happiness->amenities - happiness->demand;
        if (netAmenities >= 0.0f) { continue; }  // Happy city, no strike risk

        // Count industrial buildings
        const CityDistrictsComponent* districts =
            world.tryGetComponent<CityDistrictsComponent>(cityEntity);
        if (districts == nullptr) { continue; }

        int32_t industrialBuildings = 0;
        for (const CityDistrictsComponent::PlacedDistrict& d : districts->districts) {
            if (d.type == DistrictType::Industrial) {
                industrialBuildings += static_cast<int32_t>(d.buildings.size());
            }
        }

        if (industrialBuildings < 3) { continue; }

        // Strike trigger!
        CityStrikeComponent* strike =
            world.tryGetComponent<CityStrikeComponent>(cityEntity);
        if (strike == nullptr) {
            CityStrikeComponent newStrike{};
            world.addComponent<CityStrikeComponent>(cityEntity, std::move(newStrike));
            strike = world.tryGetComponent<CityStrikeComponent>(cityEntity);
        }
        if (strike != nullptr && !strike->isOnStrike) {
            strike->isOnStrike = true;
            strike->strikeTurnsRemaining = 3;
            LOG_INFO("LABOR STRIKE in city %s! Industrial buildings shut down for 3 turns",
                     city.name.c_str());
        }
    }
}

void processStrikes(aoc::ecs::World& world) {
    aoc::ecs::ComponentPool<CityStrikeComponent>* strikePool =
        world.getPool<CityStrikeComponent>();
    if (strikePool == nullptr) { return; }

    for (uint32_t i = 0; i < strikePool->size(); ++i) {
        CityStrikeComponent& strike = strikePool->data()[i];
        if (!strike.isOnStrike) { continue; }

        --strike.strikeTurnsRemaining;
        if (strike.strikeTurnsRemaining <= 0) {
            strike.isOnStrike = false;
            LOG_INFO("Strike ended in a city");
        }
    }
}

// ============================================================================
// Insurance
// ============================================================================

void processInsurancePremiums(aoc::ecs::World& world) {
    aoc::ecs::ComponentPool<PlayerInsuranceComponent>* insPool =
        world.getPool<PlayerInsuranceComponent>();
    aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();
    if (insPool == nullptr || monetaryPool == nullptr) { return; }

    for (uint32_t i = 0; i < insPool->size(); ++i) {
        const PlayerInsuranceComponent& ins = insPool->data()[i];
        int32_t premium = ins.totalPremium();
        if (premium <= 0) { continue; }

        for (uint32_t m = 0; m < monetaryPool->size(); ++m) {
            if (monetaryPool->data()[m].owner == ins.owner) {
                monetaryPool->data()[m].treasury -= static_cast<CurrencyAmount>(premium);
                break;
            }
        }
    }
}

// ============================================================================
// Economic Espionage
// ============================================================================

ErrorCode executeEconSpyMission(aoc::ecs::World& world,
                                 EntityId /*spyEntity*/,
                                 EconSpyMission mission) {
    switch (mission) {
        case EconSpyMission::StealRecipe:
            LOG_INFO("Economic espionage: recipe stolen!");
            break;
        case EconSpyMission::MarketManipulation:
            LOG_INFO("Economic espionage: market manipulated!");
            break;
        case EconSpyMission::InsiderTrading:
            LOG_INFO("Economic espionage: insider trading intelligence gathered!");
            break;
        case EconSpyMission::Counterfeit: {
            LOG_INFO("Economic espionage: counterfeit coins introduced!");
            // Damage target's trust
            aoc::ecs::ComponentPool<CurrencyTrustComponent>* trustPool =
                world.getPool<CurrencyTrustComponent>();
            if (trustPool != nullptr) {
                // Find the target (spy's target city's owner)
                // Simplified: just log for now
            }
            break;
        }
        default:
            break;
    }
    return ErrorCode::Ok;
}

// ============================================================================
// Migration
// ============================================================================

void processMigration(aoc::ecs::World& world, const aoc::map::HexGrid& grid) {
    const aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    if (cityPool == nullptr) { return; }

    // Compare QoL between neighboring cities across borders
    for (uint32_t a = 0; a < cityPool->size(); ++a) {
        const CityComponent& cityA = cityPool->data()[a];
        if (cityA.owner == BARBARIAN_PLAYER || cityA.owner == INVALID_PLAYER) {
            continue;
        }

        const CityHappinessComponent* happyA =
            world.tryGetComponent<CityHappinessComponent>(cityPool->entities()[a]);
        float qolA = (happyA != nullptr) ? (happyA->amenities - happyA->demand) : 0.0f;

        for (uint32_t b = a + 1; b < cityPool->size(); ++b) {
            const CityComponent& cityB = cityPool->data()[b];
            if (cityB.owner == BARBARIAN_PLAYER || cityB.owner == INVALID_PLAYER) {
                continue;
            }
            if (cityB.owner == cityA.owner) {
                continue;  // Same player, no migration
            }

            // Check if cities are near each other (within 10 hexes)
            int32_t dist = hex::distance(cityA.location, cityB.location);
            if (dist > 10) { continue; }

            const CityHappinessComponent* happyB =
                world.tryGetComponent<CityHappinessComponent>(cityPool->entities()[b]);
            float qolB = (happyB != nullptr) ? (happyB->amenities - happyB->demand) : 0.0f;

            float qolDiff = qolB - qolA;
            if (std::abs(qolDiff) < 3.0f) { continue; }

            // Check immigration policies
            aoc::ecs::ComponentPool<PlayerMigrationComponent>* migPool =
                world.getPool<PlayerMigrationComponent>();
            if (migPool == nullptr) { continue; }

            // Determine source and destination
            CityComponent* source = nullptr;
            CityComponent* dest = nullptr;
            EntityId sourceEntity = NULL_ENTITY;

            if (qolDiff > 0.0f) {
                // B is better, A loses population
                source = const_cast<CityComponent*>(&cityA);
                dest = const_cast<CityComponent*>(&cityB);
                sourceEntity = cityPool->entities()[a];
            } else {
                source = const_cast<CityComponent*>(&cityB);
                dest = const_cast<CityComponent*>(&cityA);
                sourceEntity = cityPool->entities()[b];
            }

            // Check destination's immigration policy
            ImmigrationPolicy destPolicy = ImmigrationPolicy::Controlled;
            for (uint32_t m = 0; m < migPool->size(); ++m) {
                if (migPool->data()[m].owner == dest->owner) {
                    destPolicy = migPool->data()[m].policy;
                    break;
                }
            }

            if (destPolicy == ImmigrationPolicy::Closed) { continue; }

            // Migration happens slowly: 1 citizen per 10 turns
            // Use hash for determinism
            uint32_t migHash = static_cast<uint32_t>(sourceEntity.index) * 2654435761u;
            if ((migHash % 10) != 0) { continue; }

            if (source->population > 2) {
                --source->population;
                ++dest->population;
                LOG_INFO("Migration: 1 citizen moved from %s to %s (QoL difference %.1f)",
                         source->name.c_str(), dest->name.c_str(),
                         static_cast<double>(std::abs(qolDiff)));
            }
        }
    }
}

} // namespace aoc::sim
