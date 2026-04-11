/**
 * @file ColonialEconomics.cpp
 * @brief Colonial economic zone extraction and mercantilism mechanics.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/simulation/economy/ColonialEconomics.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/CityLoyalty.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

/// Minimum GDP ratio required for colonizer vs host.
constexpr float MIN_GDP_RATIO = 2.0f;

/// Loyalty penalty per turn for having an economic zone.
constexpr float ZONE_LOYALTY_PENALTY = 2.0f;

ErrorCode establishEconomicZone(aoc::game::GameState& gameState,
                                GlobalEconomicZoneTracker& tracker,
                                PlayerId colonizer,
                                EntityId hostCity) {
    aoc::ecs::World& world = gameState.legacyWorld();
    // Check the city exists and belongs to someone else
    const CityComponent* city = world.tryGetComponent<CityComponent>(hostCity);
    if (city == nullptr || city->owner == colonizer) {
        return ErrorCode::InvalidArgument;
    }

    // Can't double-zone
    if (tracker.hasZone(hostCity)) {
        return ErrorCode::InvalidArgument;
    }

    // GDP check: colonizer must have 2x the host's GDP
    const aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();
    if (monetaryPool == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    CurrencyAmount colonizerGDP = 0;
    CurrencyAmount hostGDP = 0;
    for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
        if (monetaryPool->data()[i].owner == colonizer) {
            colonizerGDP = monetaryPool->data()[i].gdp;
        }
        if (monetaryPool->data()[i].owner == city->owner) {
            hostGDP = monetaryPool->data()[i].gdp;
        }
    }

    if (hostGDP > 0 && static_cast<float>(colonizerGDP) / static_cast<float>(hostGDP)
        < MIN_GDP_RATIO) {
        return ErrorCode::InvalidArgument;
    }

    EconomicZone zone;
    zone.colonizer = colonizer;
    zone.host = city->owner;
    zone.hostCityEntity = hostCity;
    zone.extractionRate = 0.30f;
    zone.paymentRate = 0.50f;
    zone.turnsActive = 0;
    tracker.zones.push_back(zone);

    LOG_INFO("Player %u established economic zone in player %u's city %s",
             static_cast<unsigned>(colonizer),
             static_cast<unsigned>(city->owner),
             city->name.c_str());

    return ErrorCode::Ok;
}

void dissolveEconomicZone(GlobalEconomicZoneTracker& tracker, EntityId hostCity) {
    std::vector<EconomicZone>::iterator it = tracker.zones.begin();
    while (it != tracker.zones.end()) {
        if (it->hostCityEntity == hostCity) {
            LOG_INFO("Economic zone dissolved in city entity %u",
                     static_cast<unsigned>(hostCity.index));
            it = tracker.zones.erase(it);
            return;
        }
        ++it;
    }
}

void processEconomicZones(aoc::game::GameState& gameState,
                          const aoc::map::HexGrid& /*grid*/,
                          const Market& market,
                          GlobalEconomicZoneTracker& tracker) {
    aoc::ecs::World& world = gameState.legacyWorld();
    std::vector<EconomicZone>::iterator it = tracker.zones.begin();
    while (it != tracker.zones.end()) {
        aoc::ecs::World& world = gameState.legacyWorld();
        EconomicZone& zone = *it;
        ++zone.turnsActive;

        // Verify city still exists and is owned by host
        const CityComponent* city = world.tryGetComponent<CityComponent>(zone.hostCityEntity);
        if (city == nullptr || city->owner != zone.host) {
            it = tracker.zones.erase(it);
            continue;
        }

        // Get host city stockpile
        CityStockpileComponent* hostStockpile =
            world.tryGetComponent<CityStockpileComponent>(zone.hostCityEntity);
        if (hostStockpile == nullptr) {
            ++it;
            continue;
        }

        // Extract raw resources (categories: RawStrategic, RawLuxury, RawBonus)
        CurrencyAmount totalExtractedValue = 0;
        for (std::pair<const uint16_t, int32_t>& entry : hostStockpile->goods) {
            if (entry.second <= 0) {
                continue;
            }
            const GoodDef& def = goodDef(entry.first);
            bool isRaw = def.category == GoodCategory::RawStrategic
                      || def.category == GoodCategory::RawLuxury
                      || def.category == GoodCategory::RawBonus;
            if (!isRaw) {
                continue;
            }

            int32_t extracted = static_cast<int32_t>(
                static_cast<float>(entry.second) * zone.extractionRate);
            if (extracted <= 0) {
                continue;
            }

            // Remove from host
            entry.second -= extracted;

            // Add to colonizer's first city
            aoc::ecs::ComponentPool<CityComponent>* cityPool =
                world.getPool<CityComponent>();
            if (cityPool != nullptr) {
                for (uint32_t c = 0; c < cityPool->size(); ++c) {
                    if (cityPool->data()[c].owner == zone.colonizer) {
                        EntityId colCity = cityPool->entities()[c];
                        CityStockpileComponent* colStockpile =
                            world.tryGetComponent<CityStockpileComponent>(colCity);
                        if (colStockpile != nullptr) {
                            colStockpile->addGoods(entry.first, extracted);
                        }
                        break;
                    }
                }
            }

            totalExtractedValue += static_cast<CurrencyAmount>(extracted)
                                 * static_cast<CurrencyAmount>(market.price(entry.first));
        }

        // Pay the host a fraction of market value
        if (totalExtractedValue > 0) {
            CurrencyAmount payment = static_cast<CurrencyAmount>(
                static_cast<float>(totalExtractedValue) * zone.paymentRate);

            // Find host monetary state and add payment
            aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
                world.getPool<MonetaryStateComponent>();
            if (monetaryPool != nullptr) {
                for (uint32_t m = 0; m < monetaryPool->size(); ++m) {
                    if (monetaryPool->data()[m].owner == zone.host) {
                        monetaryPool->data()[m].treasury += payment;
                        break;
                    }
                }
            }
        }

        // Reduce host city loyalty
        CityLoyaltyComponent* loyalty =
            world.tryGetComponent<CityLoyaltyComponent>(zone.hostCityEntity);
        if (loyalty != nullptr) {
            loyalty->loyalty -= ZONE_LOYALTY_PENALTY;
            loyalty->loyalty = std::max(0.0f, loyalty->loyalty);

            // If loyalty hits 0: dissolve zone (city may flip via loyalty system)
            if (loyalty->loyalty <= 0.0f) {
                LOG_INFO("Economic zone revolt! City loyalty reached 0 in player %u's city",
                         static_cast<unsigned>(zone.host));
                it = tracker.zones.erase(it);
                continue;
            }
        }

        ++it;
    }
}

} // namespace aoc::sim
