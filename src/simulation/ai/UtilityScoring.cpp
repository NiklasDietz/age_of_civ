/**
 * @file UtilityScoring.cpp
 * @brief Utility-based AI scoring for production and research decisions.
 */

#include "aoc/simulation/ai/UtilityScoring.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim {

ProductionScores computeProductionUtility(const LeaderBehavior& b, const AIContext& context) {
    ProductionScores scores{};

    // ================================================================
    // Base scores weighted by personality
    // ================================================================

    // Settler: high value when below target cities
    scores.settler = 150.0f * b.expansionism * b.prodSettlers;
    if (context.ownedCities >= context.targetMaxCities) { scores.settler = 0.0f; }
    if (context.settlerUnits > 0) { scores.settler = 0.0f; }
    if (context.totalPopulation < context.ownedCities * 2) { scores.settler *= 0.3f; }

    // Builder: needed for improvements
    scores.builder = 80.0f * b.prodBuilders;
    if (context.builderUnits > 0) { scores.builder *= 0.1f; }
    if (context.needsImprovements) { scores.builder *= 2.0f; }

    // Military: scales with aggression and threat
    scores.military = 100.0f * b.militaryAggression * b.prodMilitary;
    if (context.militaryUnits >= context.desiredMilitary) { scores.military *= 0.3f; }
    if (context.isThreatened) { scores.military *= 3.0f; }

    // Naval military
    scores.navalMilitary = 60.0f * b.prodNaval;

    // Religious units
    scores.religious = 60.0f * b.religiousZeal * b.prodReligious;

    // Wonders
    scores.wonder = 200.0f * b.cultureFocus * b.prodWonders;

    // ================================================================
    // Building categories
    // ================================================================

    // Science buildings (Library, University, Research Lab)
    scores.scienceBuilding = 120.0f * b.scienceFocus * b.prodBuildings;
    if (!context.hasCampus) { scores.scienceBuilding *= 0.3f; }  // Need Campus first

    // Economic buildings (Market, Bank, Stock Exchange)
    scores.economicBuilding = 100.0f * b.economicFocus * b.prodBuildings;
    if (!context.hasCommercial) { scores.economicBuilding *= 0.3f; }

    // Industrial buildings (Workshop, Factory, etc.)
    scores.industrialBuilding = 110.0f * b.prodBuildings * b.techIndustrial;

    // Culture buildings (Monument)
    scores.cultureBuilding = 90.0f * b.cultureFocus * b.prodBuildings;

    // Military buildings (Barracks, Walls)
    scores.militaryBuilding = 70.0f * b.militaryAggression * b.prodBuildings;

    // Mint: HIGH priority if no coins yet (enables monetary advancement)
    scores.mintBuilding = 130.0f * b.economicFocus;
    if (context.hasCoins) { scores.mintBuilding *= 0.2f; }  // Already have coins
    if (context.hasMint) { scores.mintBuilding = 0.0f; }    // Already have Mint
    if (!context.hasCommercial) { scores.mintBuilding *= 0.3f; }  // Need Commercial first

    // Power plants
    scores.powerPlant = 90.0f * b.techIndustrial * b.prodBuildings;

    // Districts: high base value since they unlock buildings
    scores.district = 140.0f * b.prodBuildings;

    return scores;
}

float scoreBuildingForLeader(const LeaderBehavior& b, BuildingId buildingId,
                              const AIContext& context) {
    float score = 50.0f;  // Base score for any building

    switch (buildingId.value) {
        // City Center
        case 15: score = 70.0f; break;   // Granary: food, always useful
        case 16: score = 80.0f * b.cultureFocus; break;  // Monument: culture
        case 22: score = 60.0f; break;   // Hospital

        // Campus
        case  7: score = 100.0f * b.scienceFocus; break;  // Library
        case 19: score = 130.0f * b.scienceFocus; break;  // University
        case 12: score = 160.0f * b.scienceFocus; break;  // Research Lab

        // Commercial
        case  6: score = 90.0f * b.economicFocus; break;   // Market
        case 20: score = 110.0f * b.economicFocus; break;  // Bank
        case 21: score = 130.0f * b.economicFocus; break;  // Stock Exchange
        case 24: score = 140.0f * b.economicFocus;          // Mint!
                 if (!context.hasCoins) { score *= 1.5f; }      // Huge bonus if no coins
                 break;

        // Industrial -- Forge and Workshop are critical: they enable the entire
        // production chain (smelting, tools, lumber, charcoal, glass).
        case  0: score = 200.0f * b.techIndustrial; break;  // Forge (smelting, tools, charcoal)
        case  1: score = 180.0f * b.techIndustrial; break;  // Workshop (lumber, bricks, construction)
        case  3: score = 150.0f * b.techIndustrial; break;  // Factory (steel, machinery)
        case  5: score = 160.0f * b.techIndustrial; break;  // Industrial Complex

        // Encampment
        case 17: score = 60.0f * b.militaryAggression; break; // Walls
        case 18: score = 70.0f * b.militaryAggression; break; // Barracks

        // Power plants
        case 26: score = 90.0f * b.techIndustrial; break;  // Coal Plant
        case 27: score = 95.0f * b.techIndustrial; break;  // Oil Plant
        case 28: score = 100.0f; break;                     // Hydroelectric (clean)
        case 29: score = 80.0f * b.techIndustrial; break;  // Nuclear (risky)
        case 30: score = 110.0f; break;                     // Solar
        case 31: score = 105.0f; break;                     // Wind

        // Other
        case  8: score = 70.0f; break;  // Textile Mill
        case  9: score = 65.0f; break;  // Food Processing
        case 25: score = 50.0f; break;  // Waste Treatment

        // Faith buildings (HolySite) -- scale by religiousZeal
        case 36: score = 100.0f * b.religiousZeal; break;  // Shrine
        case 37: score = 140.0f * b.religiousZeal; break;  // Temple
        case 38: score = 170.0f * b.religiousZeal * b.cultureFocus; break;  // Cathedral

        default: score = 40.0f; break;
    }

    // Context bonus: treasury-rich players can afford expensive buildings
    if (context.treasury > 5000) { score *= 1.2f; }

    // Pollution penalty scaled by environmentalism gene: green leaders
    // strongly avoid emitters; low-env leaders barely care. Cleaners
    // (negative emission) get a bonus proportional to the same gene.
    const int32_t pollution = buildingPollutionEmission(buildingId);
    score -= static_cast<float>(pollution) * b.environmentalism * 5.0f;

    // Great-People spawn affinity: buildings that contribute GP points get a
    // bonus scaled by greatPersonFocus times the matching category gene.
    // Campus -> Scientist, Industrial -> Engineer, Commercial -> Merchant,
    // Encampment -> General, Monument -> Artist.
    float gpBonus = 0.0f;
    switch (buildingId.value) {
        case  7: case 19: case 12:
            gpBonus = 15.0f * b.scienceFocus; break;
        case  0: case  1: case  3: case  5:
            gpBonus = 15.0f * b.techIndustrial; break;
        case  6: case 20: case 21: case 24:
            gpBonus = 12.0f * b.economicFocus; break;
        case 17: case 18:
            gpBonus = 10.0f * b.militaryAggression; break;
        case 16:
            gpBonus = 10.0f * b.cultureFocus; break;
        default: break;
    }
    score += gpBonus * b.greatPersonFocus;

    return score;
}

float scoreTechForLeader(const LeaderBehavior& b, TechId techId,
                          const AIContext& /*context*/) {
    if (!techId.isValid() || techId.value >= techCount()) {
        return 0.0f;
    }

    const TechDef& def = techDef(techId);
    float score = 100.0f;  // Base score

    // Prefer cheaper techs (lower opportunity cost)
    score -= static_cast<float>(def.researchCost) * 0.01f;

    // Bonus for techs that unlock things
    if (!def.unlockedBuildings.empty()) {
        score += 200.0f * b.prodBuildings;
        // Extra bonus for science/economic buildings
        for (BuildingId bid : def.unlockedBuildings) {
            if (bid.value == 7 || bid.value == 19 || bid.value == 12) {
                score += 150.0f * b.scienceFocus;  // Science buildings
            }
            if (bid.value == 6 || bid.value == 20 || bid.value == 24) {
                score += 120.0f * b.economicFocus;  // Economic + Mint
            }
            if (bid.value == 3 || bid.value == 5) {
                score += 100.0f * b.techIndustrial;  // Industrial
            }
        }
    }

    if (!def.unlockedUnits.empty()) {
        score += 150.0f * b.militaryAggression * b.techMilitary;
    }

    // Era-based bias
    if (def.era.value >= 5) {
        score *= b.techInformation;  // Late techs biased by info focus
    }
    if (def.era.value <= 1) {
        score *= 1.3f;  // Early techs always valuable (unlock basics)
    }

    return score;
}

} // namespace aoc::sim
