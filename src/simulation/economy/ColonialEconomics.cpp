/**
 * @file ColonialEconomics.cpp
 * @brief Colonial economic zone extraction and mercantilism mechanics.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/economy/ColonialEconomics.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/city/CityLoyalty.hpp"
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
                                aoc::hex::AxialCoord hostCityLocation, PlayerId host) {
    if (host == colonizer) {
        return ErrorCode::InvalidArgument;
    }

    if (tracker.hasZone(hostCityLocation)) {
        return ErrorCode::InvalidArgument;
    }

    const aoc::game::Player* colonizerPlayer = gameState.player(colonizer);
    const aoc::game::Player* hostPlayer      = gameState.player(host);
    if (colonizerPlayer == nullptr || hostPlayer == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    CurrencyAmount colonizerGDP = colonizerPlayer->monetary().gdp;
    CurrencyAmount hostGDP      = hostPlayer->monetary().gdp;

    if (hostGDP > 0 && static_cast<float>(colonizerGDP) / static_cast<float>(hostGDP)
        < MIN_GDP_RATIO) {
        return ErrorCode::InvalidArgument;
    }

    EconomicZone zone;
    zone.colonizer          = colonizer;
    zone.host               = host;
    zone.hostCityLocation   = hostCityLocation;
    zone.extractionRate     = 0.30f;
    zone.paymentRate        = 0.50f;
    zone.turnsActive        = 0;
    tracker.zones.push_back(zone);

    LOG_INFO("Player %u established economic zone in player %u's city",
             static_cast<unsigned>(colonizer),
             static_cast<unsigned>(host));

    return ErrorCode::Ok;
}

void dissolveEconomicZone(GlobalEconomicZoneTracker& tracker, aoc::hex::AxialCoord hostCityLocation) {
    std::vector<EconomicZone>::iterator it = tracker.zones.begin();
    while (it != tracker.zones.end()) {
        if (it->hostCityLocation == hostCityLocation) {
            LOG_INFO("Economic zone dissolved");
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
    std::vector<EconomicZone>::iterator it = tracker.zones.begin();
    while (it != tracker.zones.end()) {
        EconomicZone& zone = *it;
        ++zone.turnsActive;

        aoc::game::Player* hostPlayerObj = gameState.player(zone.host);
        if (hostPlayerObj == nullptr) { it = tracker.zones.erase(it); continue; }

        aoc::game::City* hostCityObj = hostPlayerObj->cityAt(zone.hostCityLocation);
        if (hostCityObj == nullptr) { ++it; continue; }

        CityStockpileComponent& hostStockpile = hostCityObj->stockpile();

        CurrencyAmount totalExtractedValue = 0;
        for (std::pair<const uint16_t, int32_t>& entry : hostStockpile.goods) {
            if (entry.second <= 0) { continue; }
            const GoodDef& def = goodDef(entry.first);
            const bool isRaw = def.category == GoodCategory::RawStrategic
                            || def.category == GoodCategory::RawLuxury
                            || def.category == GoodCategory::RawBonus;
            if (!isRaw) { continue; }

            const int32_t extracted = static_cast<int32_t>(
                static_cast<float>(entry.second) * zone.extractionRate);
            if (extracted <= 0) { continue; }

            entry.second -= extracted;

            // Add to colonizer's first city stockpile
            aoc::game::Player* colPlayer = gameState.player(zone.colonizer);
            if (colPlayer != nullptr) {
                for (const std::unique_ptr<aoc::game::City>& cityPtr : colPlayer->cities()) {
                    if (cityPtr == nullptr) { continue; }
                    cityPtr->stockpile().addGoods(entry.first, extracted);
                    break;
                }
            }

            totalExtractedValue += static_cast<CurrencyAmount>(extracted)
                                 * static_cast<CurrencyAmount>(market.price(entry.first));
        }

        if (totalExtractedValue > 0) {
            const CurrencyAmount payment = static_cast<CurrencyAmount>(
                static_cast<float>(totalExtractedValue) * zone.paymentRate);
            hostPlayerObj->monetary().treasury += payment;
        }

        // Reduce host city loyalty
        CityLoyaltyComponent& loyalty = hostCityObj->loyalty();
        loyalty.loyalty -= ZONE_LOYALTY_PENALTY;
        loyalty.loyalty  = std::max(0.0f, loyalty.loyalty);

        if (loyalty.loyalty <= 0.0f) {
            LOG_INFO("Economic zone revolt! City loyalty reached 0 in player %u's city",
                     static_cast<unsigned>(zone.host));
            it = tracker.zones.erase(it);
            continue;
        }

        ++it;
    }
}

} // namespace aoc::sim
