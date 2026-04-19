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
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
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

        // 1. Science from worked tiles
        for (const aoc::hex::AxialCoord& tile : city->workedTiles()) {
            if (grid.isValid(tile)) {
                aoc::map::TileYield yield = grid.tileYield(grid.toIndex(tile));
                cityScience += static_cast<float>(yield.science);
            }
        }

        // 2. Population base science (1.5 per citizen).
        // Raised from 0.5 to ensure early cities with 1-2 pop can realistically
        // advance through the tech tree within a normal game length.
        cityScience += static_cast<float>(city->population()) * 1.5f;

        // 3. Palace bonus: capital gets extra science from the seat of government.
        // Raised from 3 to 5 to give the capital a meaningful research advantage
        // that compensates for early-game lack of science buildings.
        if (city->isOriginalCapital()) {
            cityScience += 5.0f;
        }

        // 4. Building bonuses and multiplier
        float bestMultiplier = 1.0f;
        const CityDistrictsComponent& districts = city->districts();
        for (const CityDistrictsComponent::PlacedDistrict& district : districts.districts) {
            for (BuildingId bid : district.buildings) {
                if (bid.value < BUILDING_DEFS.size()) {
                    const BuildingDef& bdef = buildingDef(bid);
                    cityScience += static_cast<float>(bdef.scienceBonus);
                    bestMultiplier = std::max(bestMultiplier, bdef.scienceMultiplier);
                }
            }
        }

        // 5. Apply multiplier
        cityScience *= bestMultiplier;

        // 6. Religion-vs-education curve: net Devotion * era coefficient.
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

        totalScience += cityScience;
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

    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        // Culture from worked tiles
        for (const aoc::hex::AxialCoord& tile : city->workedTiles()) {
            if (grid.isValid(tile)) {
                aoc::map::TileYield yield = grid.tileYield(grid.toIndex(tile));
                totalCulture += static_cast<float>(yield.culture);
            }
        }

        // Population base culture (1.0 per citizen, includes implicit Monument)
        totalCulture += static_cast<float>(city->population()) * 1.0f;

        // Palace bonus: capital generates extra culture (+2, matching Civ 6)
        if (city->isOriginalCapital()) {
            totalCulture += 2.0f;
        }

        // Building culture (Theatre Square district buildings)
        const CityDistrictsComponent& districts = city->districts();
        for (const CityDistrictsComponent::PlacedDistrict& district : districts.districts) {
            for (BuildingId bid : district.buildings) {
                if (bid.value < BUILDING_DEFS.size()) {
                    totalCulture += static_cast<float>(buildingDef(bid).cultureBonus);
                }
            }
        }
    }

    // Apply civilization culture multiplier
    totalCulture *= civDef(player.civId()).modifiers.cultureMultiplier;

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
