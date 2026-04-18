/**
 * @file Happiness.cpp
 * @brief City happiness calculation using GameState object model.
 *
 * Migrated to GameState (Player/City/Unit).
 * All component data is read directly from the object model, no ECS pools.
 */

#include "aoc/simulation/city/Happiness.hpp"
#include "aoc/simulation/city/District.hpp"

#include <cmath>
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/monetary/Inflation.hpp"
#include "aoc/simulation/monetary/FiscalPolicy.hpp"
#include "aoc/simulation/wonder/Wonder.hpp"
#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/simulation/diplomacy/WarWeariness.hpp"
#include "aoc/simulation/government/Government.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"

#include <cmath>

namespace aoc::sim {

void computeCityHappiness(aoc::game::Player& player) {
    // War weariness penalty
    float warWearinessPenalty = warWearinessHappinessPenalty(
        player.warWeariness().weariness);

    // Inflation and tax penalties from monetary state
    float inflationPenalty = inflationHappinessPenalty(player.monetary().inflationRate);
    float taxPenalty = -taxHappinessModifier(player.monetary().taxRate);

    // Gather unique luxury resource types across ALL player cities (deduplication).
    constexpr uint16_t RAW_LUXURY_IDS[] = {
        goods::WINE, goods::SPICES, goods::SILK, goods::IVORY, goods::GEMS,
        goods::DYES, goods::FURS, goods::INCENSE, goods::SUGAR,
        goods::PEARLS, goods::TEA, goods::COFFEE, goods::TOBACCO
    };
    int32_t uniqueLuxuryCount = 0;
    for (uint16_t luxId : RAW_LUXURY_IDS) {
        bool playerHasThis = false;
        for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
            if (city->stockpile().getAmount(luxId) > 0) {
                playerHasThis = true;
                break;
            }
        }
        if (playerHasThis) {
            ++uniqueLuxuryCount;
        }
    }

    int32_t playerCityCount = player.cityCount();

    // Each unique luxury provides +1 amenity to each city, up to 4 cities per luxury.
    float luxuryAmenityPerCity = 0.0f;
    if (playerCityCount > 0) {
        float totalPool = static_cast<float>(uniqueLuxuryCount) * 4.0f;
        luxuryAmenityPerCity = std::min(
            static_cast<float>(uniqueLuxuryCount),
            totalPool / static_cast<float>(playerCityCount));
    }

    // Government data for empire size penalty and military unhappiness
    const GovernmentDef& gdef = governmentDef(player.government().government);

    // Count military units away from cities (for military unhappiness)
    int32_t unitsAway = 0;
    if (gdef.militaryUnhappyFactor > 0.0f) {
        for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
            if (!unit->isMilitary()) { continue; }
            bool inCity = false;
            for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
                if (city->location() == unit->position()) {
                    inCity = true;
                    break;
                }
            }
            if (!inCity) { ++unitsAway; }
        }
    }
    float militaryUnhappyPerCity = 0.0f;
    if (playerCityCount > 0 && gdef.militaryUnhappyFactor > 0.0f) {
        float totalMilUnhappy = static_cast<float>(unitsAway) * gdef.militaryUnhappyFactor;
        militaryUnhappyPerCity = totalMilUnhappy / static_cast<float>(playerCityCount);
    }

    // Empire size penalty
    int32_t excessCities = playerCityCount - gdef.empireSizeThreshold;
    float empirePenalty = (excessCities > 0) ? static_cast<float>(excessCities) * 0.5f : 0.0f;

    // Process each city
    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        CityHappinessComponent& happiness = city->happiness();

        // Base amenities: 1 from palace/capital
        happiness.amenities = 1.0f;

        // Luxury allocation slider bonus
        happiness.amenities += player.monetary().luxuryAllocation * 5.0f;

        // Deduplicated luxury amenities
        happiness.amenities += luxuryAmenityPerCity;

        // Processed goods happiness: having consumer goods, food, clothing, and
        // electronics in the city stockpile = citizens are well-supplied.
        // Scales with quantity (diminishing returns via sqrt) so producing MORE
        // goods of each type makes your cities happier — driving demand for the
        // entire supply chain. This is the key incentive for industrialization.
        const CityStockpileComponent& stockpile = city->stockpile();
        {
            // auto required: lambda type is unnameable
            auto goodsHappiness = [&stockpile](uint16_t goodId, float baseBonus) -> float {
                const int32_t amount = stockpile.getAmount(goodId);
                if (amount <= 0) { return 0.0f; }
                // sqrt scaling: 1 unit = baseBonus, 4 units = 2x, 9 units = 3x, capped at 4x
                return std::min(baseBonus * std::sqrt(static_cast<float>(amount)), baseBonus * 4.0f);
            };
            happiness.amenities += goodsHappiness(goods::CONSUMER_GOODS, 0.5f);
            happiness.amenities += goodsHappiness(goods::CLOTHING, 0.7f);
            happiness.amenities += goodsHappiness(goods::ADV_CONSUMER_GOODS, 1.0f);
            happiness.amenities += goodsHappiness(goods::PROCESSED_FOOD, 0.3f);
            // Electronics (ID 75) represent modern quality of life
            happiness.amenities += goodsHappiness(75, 0.8f);
        }

        // Specialist entertainers: +2 amenity each
        happiness.amenities += static_cast<float>(city->entertainers()) * 2.0f;

        // Building amenities
        const CityDistrictsComponent& districts = city->districts();
        for (const CityDistrictsComponent::PlacedDistrict& d : districts.districts) {
            if (d.type != DistrictType::CityCenter) {
                happiness.amenities += 0.5f;
            }
            for (BuildingId bid : d.buildings) {
                if (bid.value == 15) { happiness.amenities += 0.5f; }  // Granary
                if (bid.value == 22) { happiness.amenities += 1.0f; }  // Hospital
                if (bid.value == 6)  { happiness.amenities += 0.5f; }  // Market
                if (bid.value == 16) { happiness.amenities += 0.5f; }  // Monument
            }
        }

        // Wonder amenities
        const CityWondersComponent& cityWonders = city->wonders();
        for (const WonderId wid : cityWonders.wonders) {
            const WonderDef& wdef = wonderDef(wid);
            happiness.amenities += wdef.effect.amenityBonus;
        }

        // Religion follower belief amenity bonus
        const CityReligionComponent& cityReligion = city->religion();
        ReligionId dominant = cityReligion.dominantReligion();
        if (dominant != NO_RELIGION) {
            // Religion tracker is global - needs to be passed or accessed differently.
            // For now, skip religion bonus (will be added when global state is in GameState)
        }

        // Empire size penalty
        happiness.amenities -= empirePenalty;

        // Military unhappiness
        happiness.amenities -= militaryUnhappyPerCity;

        // Demand: scales sub-linearly with population
        happiness.demand = std::sqrt(static_cast<float>(city->population())) * 0.8f;

        // Modifiers from economy and war weariness
        happiness.modifiers = -inflationPenalty - taxPenalty + warWearinessPenalty;

        // Pollution amenity penalty
        happiness.amenities -= static_cast<float>(city->pollution().amenityPenalty());

        // Net happiness
        happiness.happiness = happiness.amenities - happiness.demand + happiness.modifiers;
    }
}

} // namespace aoc::sim
