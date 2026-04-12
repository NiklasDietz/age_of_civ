/**
 * @file HumanCapital.cpp
 * @brief Education, literacy, and human capital development.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/economy/HumanCapital.hpp"
#include "aoc/simulation/city/District.hpp"

#include <algorithm>

namespace aoc::sim {

void updateHumanCapital(aoc::game::GameState& gameState, PlayerId player) {
    aoc::game::Player* playerObj = gameState.player(player);
    if (playerObj == nullptr) {
        return;
    }

    PlayerHumanCapitalComponent& hc = playerObj->humanCapital();

    int32_t totalPopulation    = 0;
    int32_t educationCapacity  = 0;

    for (const std::unique_ptr<aoc::game::City>& cityPtr : playerObj->cities()) {
        if (cityPtr == nullptr) { continue; }
        totalPopulation += cityPtr->population();

        for (const CityDistrictsComponent::PlacedDistrict& d : cityPtr->districts().districts) {
            if (d.type == DistrictType::Campus) {
                educationCapacity += 2;  // Campus district base: +2
                for (BuildingId bid : d.buildings) {
                    switch (bid.value) {
                        case 7:  educationCapacity += 3; break;  // Library
                        case 19: educationCapacity += 5; break;  // University
                        case 12: educationCapacity += 8; break;  // Research Lab
                        default: break;
                    }
                }
            }
            for (BuildingId bid : d.buildings) {
                if (bid.value == 16) { educationCapacity += 1; }  // Monument: oral tradition
            }
        }
    }

    if (totalPopulation <= 0) {
        return;
    }

    float targetLiteracy = static_cast<float>(educationCapacity)
                         / static_cast<float>(totalPopulation);
    targetLiteracy = std::clamp(targetLiteracy, 0.02f, 1.0f);

    float diff = targetLiteracy - hc.literacyRate;
    if (diff > 0.0f) {
        hc.literacyRate += diff * 0.05f;   // Growing: 5% per turn toward target
    } else {
        hc.literacyRate += diff * 0.02f;   // Decaying: 2% per turn toward target
    }

    hc.literacyRate = std::clamp(hc.literacyRate, 0.02f, 1.0f);
}

} // namespace aoc::sim
