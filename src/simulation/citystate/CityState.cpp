/**
 * @file CityState.cpp
 * @brief City-state spawning, envoy processing, and per-turn bonuses.
 */

#include "aoc/simulation/citystate/CityState.hpp"

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/city/BorderExpansion.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"

#include <algorithm>
#include <vector>

namespace aoc::sim {

void spawnCityStates(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                      int32_t count, aoc::Random& rng) {
    const int32_t toSpawn = std::min(count, static_cast<int32_t>(CITY_STATE_COUNT));

    // Collect existing city and unit positions to enforce minimum spacing.
    std::vector<hex::AxialCoord> occupiedPositions;
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        for (const std::unique_ptr<aoc::game::City>& city : playerPtr->cities()) {
            occupiedPositions.push_back(city->location());
        }
        for (const std::unique_ptr<aoc::game::Unit>& unit : playerPtr->units()) {
            occupiedPositions.push_back(unit->position());
        }
    }
    // Include already-spawned city-state locations stored in gameState.
    for (const CityStateComponent& cs : gameState.cityStates()) {
        occupiedPositions.push_back(cs.location);
    }

    const int32_t width = grid.width();
    const int32_t height = grid.height();
    constexpr int32_t MIN_DISTANCE = 8;

    for (int32_t csIdx = 0; csIdx < toSpawn; ++csIdx) {
        const CityStateDef& csDef = CITY_STATE_DEFS[static_cast<std::size_t>(csIdx)];
        const PlayerId csPlayer = static_cast<PlayerId>(CITY_STATE_PLAYER_BASE + csIdx);

        hex::AxialCoord bestPos{0, 0};
        bool found = false;

        for (int32_t attempt = 0; attempt < 200; ++attempt) {
            const int32_t col = rng.nextInt(2, width - 3);
            const int32_t row = rng.nextInt(2, height - 3);
            const int32_t index = row * width + col;

            if (aoc::map::isWater(grid.terrain(index)) ||
                aoc::map::isImpassable(grid.terrain(index))) {
                continue;
            }

            const hex::AxialCoord candidate = hex::offsetToAxial({col, row});

            bool tooClose = false;
            for (const hex::AxialCoord& occupied : occupiedPositions) {
                if (hex::distance(candidate, occupied) < MIN_DISTANCE) {
                    tooClose = true;
                    break;
                }
            }
            if (tooClose) {
                continue;
            }

            bestPos = candidate;
            found = true;
            break;
        }

        if (!found) {
            LOG_INFO("Could not place city-state %.*s",
                     static_cast<int>(csDef.name.size()), csDef.name.data());
            continue;
        }

        // Register city-state metadata in the GameState collection.
        CityStateComponent csComp{};
        csComp.defId    = csDef.id;
        csComp.type     = csDef.type;
        csComp.location = bestPos;
        csComp.envoys.fill(0);
        csComp.suzerain = INVALID_PLAYER;
        gameState.cityStates().push_back(csComp);

        // Create the city-state's city via the player object model.
        // City-state players are allocated in the player list by the caller;
        // if that player slot exists we use it, otherwise we skip city creation.
        aoc::game::Player* csPlayerObj = gameState.player(csPlayer);
        if (csPlayerObj != nullptr) {
            aoc::game::City& csCity = csPlayerObj->addCity(bestPos, std::string(csDef.name));
            csCity.setPopulation(3);

            // Seed city-center district.
            CityDistrictsComponent::PlacedDistrict center;
            center.type     = DistrictType::CityCenter;
            center.location = bestPos;
            csCity.districts().districts.push_back(std::move(center));

            claimInitialTerritory(grid, bestPos, csPlayer);

            // Spawn a warrior adjacent to the city-state.
            const std::array<hex::AxialCoord, 6> neighbors = hex::neighbors(bestPos);
            hex::AxialCoord warriorPos = bestPos;
            for (const hex::AxialCoord& nbr : neighbors) {
                if (grid.isValid(nbr) && grid.movementCost(grid.toIndex(nbr)) > 0) {
                    warriorPos = nbr;
                    break;
                }
            }
            csPlayerObj->addUnit(UnitTypeId{0}, warriorPos);
        }

        occupiedPositions.push_back(bestPos);

        LOG_INFO("Spawned city-state %.*s at (%d,%d) player=%u",
                 static_cast<int>(csDef.name.size()), csDef.name.data(),
                 bestPos.q, bestPos.r, static_cast<unsigned>(csPlayer));
    }
}

void processCityStateBonuses(aoc::game::GameState& gameState, PlayerId player) {
    const std::vector<CityStateComponent>& cityStates = gameState.cityStates();
    if (cityStates.empty()) {
        return;
    }

    aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) {
        return;
    }
    PlayerEconomyComponent& econ = gsPlayer->economy();

    for (const CityStateComponent& cs : cityStates) {
        if (player >= MAX_PLAYERS) {
            continue;
        }
        const int8_t envoyCount = cs.envoys[player];
        if (envoyCount <= 0) {
            continue;
        }

        // Bonus magnitude: 1 envoy = +1, 3 = +2, 6 = +3.
        int32_t bonusMagnitude = 0;
        if (envoyCount >= 6) {
            bonusMagnitude = 3;
        } else if (envoyCount >= 3) {
            bonusMagnitude = 2;
        } else {
            bonusMagnitude = 1;
        }

        const CurrencyAmount bonus = static_cast<CurrencyAmount>(bonusMagnitude);
        switch (cs.type) {
            case CityStateType::Militaristic:
                econ.treasury += bonus;
                break;
            case CityStateType::Scientific:
                econ.treasury += bonus;
                break;
            case CityStateType::Cultural:
                econ.treasury += bonus;
                break;
            case CityStateType::Trade:
                econ.treasury += bonus * 2;
                break;
            case CityStateType::Religious:
                econ.treasury += bonus;
                break;
            case CityStateType::Industrial:
                econ.treasury += bonus;
                break;
            default:
                break;
        }
    }
}

} // namespace aoc::sim
