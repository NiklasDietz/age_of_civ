/**
 * @file CityScience.cpp
 * @brief Science and culture computation from cities.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/simulation/city/CityScience.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/government/GovernmentComponent.hpp"
#include "aoc/simulation/government/Government.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/monetary/Inflation.hpp"
#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/simulation/tech/EraProgression.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/wonder/Wonder.hpp"
#include "aoc/simulation/map/Improvement.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"

#include <algorithm>

namespace aoc::sim {

// ============================================================================
// GameState-native implementations
// ============================================================================

float computePlayerScience(const aoc::game::Player& player,
                            const aoc::map::HexGrid& grid) {
    float totalScience = 0.0f;

    // Precompute per-player religion curve inputs.  Derived from researched
    // techs rather than PlayerEraComponent::currentEra because the latter is
    // not reliably updated during normal play.
    const EraId playerEra = effectiveEraFromTech(player);
    const int32_t renaissancePlusTechs = countRenaissancePlusTechs(player);
    const float religionScienceCoef =
        religionScienceCoefficient(playerEra, renaissancePlusTechs);

    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        float cityScience = 0.0f;

        // 1. Science from worked tiles (WP-G adjacency cluster bonuses included).
        const aoc::sim::CivilizationDef& civModSpec = aoc::sim::civDef(player.civId());
        for (const aoc::hex::AxialCoord& tile : city->workedTiles()) {
            if (grid.isValid(tile)) {
                const int32_t tIdx = grid.toIndex(tile);
                aoc::map::TileYield yield = effectiveTileYield(grid, tIdx);
                cityScience += static_cast<float>(yield.science);
                // Conditional: scienceFromMine on Mine improvement.
                if (civModSpec.modifiers.scienceFromMine > 0
                 && grid.improvement(tIdx) == aoc::map::ImprovementType::Mine) {
                    cityScience += static_cast<float>(civModSpec.modifiers.scienceFromMine);
                }
            }
        }

        // 2. Population base science (2.5 per citizen).
        // 2026-05-02: 1.5 → 2.5. Audit showed median civ ends with only 17
        // researched techs across a 1000-turn game; era-4 rev gates (11/18/21)
        // were unreachable for ~80% of civs. SCIENCE-win dominance from this
        // bump is countered by raising the space-race threshold (3/5 → 4/5)
        // in VictoryCondition.cpp.
        cityScience += static_cast<float>(city->population()) * 2.5f;

        // 3. Palace bonus: capital gets extra science from the seat of government.
        // Raised from 3 to 5 to give the capital a meaningful research advantage
        // that compensates for early-game lack of science buildings.
        if (city->isOriginalCapital()) {
            cityScience += 5.0f;
        }

        // 4. Building bonuses and multiplier
        float bestMultiplier = 1.0f;
        const CivilizationDef& civSpec = civDef(player.civId());
        const CityDistrictsComponent& districts = city->districts();
        for (const CityDistrictsComponent::PlacedDistrict& district : districts.districts) {
            for (BuildingId bid : district.buildings) {
                if (bid.value < BUILDING_DEFS.size()) {
                    const BuildingDef& bdef = buildingDef(bid);
                    cityScience += static_cast<float>(bdef.scienceBonus);
                    bestMultiplier = std::max(bestMultiplier, bdef.scienceMultiplier);
                    if (civSpec.uniqueBuilding.baseBuilding == bid) {
                        cityScience += static_cast<float>(civSpec.uniqueBuilding.scienceBonus);
                    }
                }
            }

            // 4b. Campus adjacency science bonus (B2). Mirrors the grid-only
            // subset of computeAdjacencyBonus so Campus districts contribute
            // science from adjacent mountains/rainforests/natural wonders.
            if (district.type == DistrictType::Campus && grid.isValid(district.location)) {
                const aoc::hex::AxialCoord center = district.location;
                const std::array<aoc::hex::AxialCoord, 6> neighbors =
                    aoc::hex::neighbors(center);
                int32_t adjMountains = 0;
                int32_t adjRainforests = 0;
                int32_t adjWonders = 0;
                for (const aoc::hex::AxialCoord& nbr : neighbors) {
                    if (!grid.isValid(nbr)) { continue; }
                    const int32_t nbrIdx = grid.toIndex(nbr);
                    if (grid.terrain(nbrIdx) == aoc::map::TerrainType::Mountain) {
                        ++adjMountains;
                    }
                    if (grid.feature(nbrIdx) == aoc::map::FeatureType::Jungle) {
                        ++adjRainforests;
                    }
                    if (grid.naturalWonder(nbrIdx) != aoc::map::NaturalWonderType::None) {
                        ++adjWonders;
                    }
                }
                cityScience += static_cast<float>(adjMountains) * 1.0f;
                cityScience += static_cast<float>(adjRainforests) * 0.5f;
                cityScience += static_cast<float>(adjWonders) * 2.0f;
            }
        }

        // 5. Apply multiplier
        cityScience *= bestMultiplier;

        // 6. Wonder science bonus (H4.9). Flat bonus per wonder applied
        // before the religion curve so Great Library etc. count as building
        // science, not a religion modifier.
        for (const WonderId wid : city->wonders().wonders) {
            const WonderDef& wdef = wonderDef(wid);
            cityScience += wdef.effect.scienceBonus
                         * wonderEraDecayFactor(wdef, player.era().currentEra);
        }

        // 7. Religion-vs-education curve: net Devotion * era coefficient.
        // Coefficient is positive in Ancient/Classical (monastic literacy
        // bonus), zero in Medieval, and negative from Renaissance onward.
        // City can offset the drain by building Library/University/Research
        // Lab, which deduct from Devotion before the coefficient is applied.
        {
            const float netDevotion = computeCityNetDevotion(*city);
            if (netDevotion > 0.0f) {
                cityScience += netDevotion * religionScienceCoef;
            }
        }

        // Civ-6 style unique improvement + district science bonus per city.
        cityScience += static_cast<float>(civSpec.uniqueImprovement.scienceBonus);

        totalScience += cityScience;
    }

    // Conditional civ bonus: +N science per active trade route.
    {
        const aoc::sim::CivilizationDef& cs = aoc::sim::civDef(player.civId());
        if (cs.modifiers.scienceFromTradeRoute > 0) {
            int32_t routes = 0;
            for (const std::unique_ptr<aoc::game::Unit>& u : player.units()) {
                if (u->typeDef().unitClass == aoc::sim::UnitClass::Trader
                 && u->trader().owner != INVALID_PLAYER) { ++routes; }
            }
            totalScience += static_cast<float>(routes * cs.modifiers.scienceFromTradeRoute);
        }
    }

    // Apply government science multiplier
    GovernmentModifiers govMods = computeGovernmentModifiers(player.government());
    totalScience *= govMods.scienceMultiplier;

    // Apply civilization science multiplier
    totalScience *= civDef(player.civId()).modifiers.scienceMultiplier;

    // Economic stability bonus
    totalScience *= economicStabilityMultiplier(player.monetary());

    // Science allocation slider bonus: fraction of city output allocated to research
    totalScience *= (1.0f + player.monetary().scienceAllocation);

    return totalScience;
}

float computePlayerCulture(const aoc::game::Player& player,
                            const aoc::map::HexGrid& grid) {
    float totalCulture = 0.0f;

    const aoc::sim::CivilizationDef& cultCivSpec = aoc::sim::civDef(player.civId());
    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        // Culture from worked tiles
        for (const aoc::hex::AxialCoord& tile : city->workedTiles()) {
            if (grid.isValid(tile)) {
                const int32_t tIdx = grid.toIndex(tile);
                aoc::map::TileYield yield = grid.tileYield(tIdx);
                totalCulture += static_cast<float>(yield.culture);
                // Conditional culture-from-feature bonuses (Brazil/Vietnam etc).
                const aoc::map::FeatureType ft = grid.feature(tIdx);
                if (cultCivSpec.modifiers.cultureFromForest > 0
                 && ft == aoc::map::FeatureType::Forest) {
                    totalCulture += static_cast<float>(cultCivSpec.modifiers.cultureFromForest);
                }
                if (cultCivSpec.modifiers.cultureFromRainforest > 0
                 && ft == aoc::map::FeatureType::Jungle) {
                    totalCulture += static_cast<float>(cultCivSpec.modifiers.cultureFromRainforest);
                }
            }
        }

        // Population base culture (1.0 per citizen, includes implicit Monument)
        totalCulture += static_cast<float>(city->population()) * 1.0f;

        // Palace bonus: capital generates extra culture (+2, matching Civ 6)
        if (city->isOriginalCapital()) {
            totalCulture += 2.0f;
        }

        // Building culture (Theatre Square district buildings)
        const CivilizationDef& civSpec = civDef(player.civId());
        const CityDistrictsComponent& districts = city->districts();
        for (const CityDistrictsComponent::PlacedDistrict& district : districts.districts) {
            for (BuildingId bid : district.buildings) {
                if (bid.value < BUILDING_DEFS.size()) {
                    totalCulture += static_cast<float>(buildingDef(bid).cultureBonus);
                    if (civSpec.uniqueBuilding.baseBuilding == bid) {
                        totalCulture += static_cast<float>(civSpec.uniqueBuilding.cultureBonus);
                    }
                }
            }
        }

        // Wonder culture bonus (H4.9): Eiffel Tower, Forbidden City, etc.
        // WP-A7: era-decay so earlier-era wonders still contribute late game
        // but stop dominating over modern additions.
        for (const WonderId wid : city->wonders().wonders) {
            const WonderDef& wdef = wonderDef(wid);
            totalCulture += wdef.effect.cultureBonus
                          * wonderEraDecayFactor(wdef, player.era().currentEra);
        }

        // Civ unique improvement culture bonus per city.
        totalCulture += static_cast<float>(civSpec.uniqueImprovement.cultureBonus);
    }

    // Conditional civ bonus: +N culture per active trade route.
    {
        const aoc::sim::CivilizationDef& cs = aoc::sim::civDef(player.civId());
        if (cs.modifiers.cultureFromTradeRoute > 0) {
            int32_t routes = 0;
            for (const std::unique_ptr<aoc::game::Unit>& u : player.units()) {
                if (u->typeDef().unitClass == aoc::sim::UnitClass::Trader
                 && u->trader().owner != INVALID_PLAYER) { ++routes; }
            }
            totalCulture += static_cast<float>(routes * cs.modifiers.cultureFromTradeRoute);
        }
    }

    // Apply civilization culture multiplier
    totalCulture *= civDef(player.civId()).modifiers.cultureMultiplier;

    // Government culture multiplier (policy cards)
    {
        GovernmentModifiers gov = computeGovernmentModifiers(player.government());
        totalCulture *= gov.cultureMultiplier;
    }

    // Economic stability bonus
    totalCulture *= economicStabilityMultiplier(player.monetary());

    return totalCulture;
}

// ============================================================================
// GameState overloads (delegate to Player versions)
// ============================================================================

float computePlayerScience(const aoc::game::GameState& gameState,
                            const aoc::map::HexGrid& grid,
                            PlayerId player) {
    const aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) {
        return 0.0f;
    }
    return computePlayerScience(*gsPlayer, grid);
}

float computePlayerCulture(const aoc::game::GameState& gameState,
                            const aoc::map::HexGrid& grid,
                            PlayerId player) {
    const aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) {
        return 0.0f;
    }
    return computePlayerCulture(*gsPlayer, grid);
}

} // namespace aoc::sim
