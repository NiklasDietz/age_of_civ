/**
 * @file Happiness.cpp
 * @brief City happiness calculation using GameState object model.
 *
 * Migrated to GameState (Player/City/Unit).
 * All component data is read directly from the object model, no ECS pools.
 */

#include "aoc/simulation/city/Happiness.hpp"
#include "aoc/simulation/city/District.hpp"

#include <algorithm>
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

int32_t luxuryTypesHeld(const aoc::game::Player& player) {
    int32_t held = 0;
    for (const uint16_t luxId : luxuryGoodIds()) {
        for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
            if (city->stockpile().getAmount(luxId) > 0) {
                ++held;
                break;
            }
        }
    }
    return held;
}

int32_t luxuryVarietyTarget(const aoc::game::Player& player) {
    return 2 + static_cast<int32_t>(player.era().currentEra.value) + player.ownedCityCount() / 2;
}

float luxuryShortfallPenalty(int32_t held, int32_t target) {
    const int32_t missing = std::max(0, target - held);
    return std::min(LUXURY_SHORTFALL_PENALTY_CAP,
                    static_cast<float>(missing) * LUXURY_SHORTFALL_PENALTY_PER_TYPE);
}

void computeCityHappiness(aoc::game::Player& player, const GlobalReligionTracker* tracker) {
    // War weariness penalty
    float warWearinessPenalty = warWearinessHappinessPenalty(player.warWeariness().weariness);

    // Inflation and tax penalties from monetary state
    float inflationPenalty = inflationHappinessPenalty(player.monetary().inflationRate);
    float taxPenalty       = -taxHappinessModifier(player.monetary().taxRate);

    // Luxury variety. Each distinct luxury type the empire holds is one
    // amenity per city (a type covers four cities), and a shortfall against
    // what the era expects costs a little. Quantity buys nothing: three units
    // of one type used to be worth a second type, so a one-off gift replaced
    // a trading relationship.
    const int32_t varietyHeld     = luxuryTypesHeld(player);
    const int32_t playerCityCount = player.ownedCityCount();
    float luxuryAmenityPerCity    = 0.0f;
    if (playerCityCount > 0) {
        const float totalPool = static_cast<float>(varietyHeld) * LUXURY_CITIES_PER_TYPE;
        luxuryAmenityPerCity  = std::min(static_cast<float>(varietyHeld),
                                         totalPool / static_cast<float>(playerCityCount));
    }
    const float varietyPenalty =
        luxuryShortfallPenalty(varietyHeld, luxuryVarietyTarget(player));

    // Government data for empire size penalty and military unhappiness
    const GovernmentDef& gdef = governmentDef(player.government().government);

    // Count military units away from cities (for military unhappiness)
    int32_t unitsAway = 0;
    if (gdef.militaryUnhappyFactor > 0.0f) {
        for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
            if (!unit->isMilitary()) {
                continue;
            }
            bool inCity = false;
            for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
                if (city->location() == unit->position()) {
                    inCity = true;
                    break;
                }
            }
            if (!inCity) {
                ++unitsAway;
            }
        }
    }
    float militaryUnhappyPerCity = 0.0f;
    if (playerCityCount > 0 && gdef.militaryUnhappyFactor > 0.0f) {
        float totalMilUnhappy  = static_cast<float>(unitsAway) * gdef.militaryUnhappyFactor;
        militaryUnhappyPerCity = totalMilUnhappy / static_cast<float>(playerCityCount);
    }

    // Empire size penalty
    int32_t excessCities = playerCityCount - gdef.empireSizeThreshold;
    float empirePenalty  = (excessCities > 0) ? static_cast<float>(excessCities) * 0.5f : 0.0f;

    // Process each city
    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        CityHappinessComponent& happiness = city->happiness();

        // Base amenities: 1 from palace/capital
        happiness.amenities = 1.0f;

        // Luxury allocation slider bonus
        happiness.amenities += player.monetary().luxuryAllocation * LUXURY_SLIDER_AMENITIES;

        // Deduplicated luxury amenities
        happiness.amenities += luxuryAmenityPerCity - varietyPenalty;

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
                if (amount <= 0) {
                    return 0.0f;
                }
                // sqrt scaling with a cap. The cap was 4x, which one unit in
                // four reaches and everything past is worth nothing -- the
                // marginal unit of a finished good stopped counting almost
                // immediately, which is half of why hoarding beat consuming.
                // Raised so a genuinely well-supplied city is distinguishable
                // from a barely-supplied one; still capped, because unbounded
                // amenities from a stockpile would make happiness a
                // warehousing exercise.
                return std::min(baseBonus * std::sqrt(static_cast<float>(amount)),
                                baseBonus * GOODS_AMENITY_CAP_MULTIPLE);
            };
            happiness.amenities += goodsHappiness(goods::CONSUMER_GOODS, 0.5f);
            happiness.amenities += goodsHappiness(goods::CLOTHING, 0.7f);
            happiness.amenities += goodsHappiness(goods::ADV_CONSUMER_GOODS, 1.0f);
            happiness.amenities += goodsHappiness(goods::PROCESSED_FOOD, 0.3f);
            // Electronics raise modern quality of life
            happiness.amenities += goodsHappiness(goods::ELECTRONICS, 0.8f);
        }

        // Whether the city's consumer demand was actually MET, as distinct from
        // how much it happens to be sitting on. The stockpile bonuses above
        // reward hoarding and saturate almost at once; this rewards the goods
        // being consumed, and penalises a city that ran short. Half the swing
        // up, half down, so meeting demand is a reward and failing it is a
        // penalty rather than merely the absence of one.
        {
            const float satisfaction = std::clamp(happiness.consumerSatisfaction, 0.0f, 1.0f);
            happiness.amenities +=
                (satisfaction - 0.5f) * CONSUMER_SATISFACTION_AMENITIES;
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
                happiness.amenities += buildingAmenities(bid);
            }
        }

        // Wonder amenities. WP-A7: era-decay.
        const CityWondersComponent& cityWonders = city->wonders();
        for (const WonderId wid : cityWonders.wonders) {
            const WonderDef& wdef = wonderDef(wid);
            happiness.amenities +=
                wdef.effect.amenityBonus * wonderEraDecayFactor(wdef, player.era().currentEra);
        }

        // A7 Colosseum (id 2) unique effect: radiates +2 amenities to every
        // same-owner city within 6 hexes of the Colosseum host. Applied
        // additively on top of the host's own amenityBonus.
        for (const std::unique_ptr<aoc::game::City>& host : player.cities()) {
            if (host.get() == city.get()) {
                continue;
            }
            if (!host->wonders().hasWonder(static_cast<aoc::sim::WonderId>(2))) {
                continue;
            }
            if (aoc::hex::distance(city->location(), host->location()) <= 6) {
                const WonderDef& wdef = wonderDef(static_cast<aoc::sim::WonderId>(2));
                const float decay     = wonderEraDecayFactor(wdef, player.era().currentEra);
                happiness.amenities += 2.0f * decay;
                break;
            }
        }

        // Religion follower belief amenity bonus
        const CityReligionComponent& cityReligion = city->religion();
        const ReligionId dominant                 = cityReligion.dominantReligion();
        if (dominant != NO_RELIGION && tracker != nullptr) {
            if (dominant < tracker->religionsFoundedCount) {
                const ReligionDef& faith = tracker->religions[dominant];
                if (faith.followerBelief < BELIEF_COUNT) {
                    // The follower belief of whatever religion holds this city.
                    happiness.amenities += allBeliefs()[faith.followerBelief].amenityBonus;
                }
            }
        }

        // Empire size penalty
        happiness.amenities -= empirePenalty;

        // Military unhappiness
        happiness.amenities -= militaryUnhappyPerCity;

        // Demand: scales sub-linearly with population
        happiness.demand = std::sqrt(static_cast<float>(city->population())) * 0.8f;

        // Modifiers from economy and war weariness. WP-A4 disaster
        // unhappiness decays 10%/turn and folds in here so climate damage
        // has a persistent-but-recoverable feel.
        happiness.disasterUnhappiness *= 0.90f;
        if (happiness.disasterUnhappiness < 0.05f) {
            happiness.disasterUnhappiness = 0.0f;
        }
        happiness.modifiers =
            -inflationPenalty - taxPenalty + warWearinessPenalty - happiness.disasterUnhappiness;

        // Pollution amenity penalty
        happiness.amenities -= static_cast<float>(city->pollution().amenityPenalty());

        // Amenity pool floors at 0. Empire/military/pollution penalties should
        // not flip the pool negative; that role belongs to the modifiers channel
        // which feeds directly into happiness below.
        if (happiness.amenities < 0.0f) {
            happiness.modifiers += happiness.amenities;
            happiness.amenities = 0.0f;
        }

        // Net happiness
        happiness.happiness = happiness.amenities - happiness.demand + happiness.modifiers;
    }
}

} // namespace aoc::sim
