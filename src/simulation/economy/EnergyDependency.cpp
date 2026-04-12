/**
 * @file EnergyDependency.cpp
 * @brief Oil/gas scarcity, energy dependency, and peak oil mechanics.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/economy/EnergyDependency.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

void updateEnergyDependency(PlayerEnergyComponent& energy,
                             int32_t oilConsumed,
                             int32_t renewableBuildingCount) {
    energy.oilConsumedThisTurn = oilConsumed;
    energy.renewableCapacity   = renewableBuildingCount;

    if (oilConsumed > 0) {
        float growthRate = static_cast<float>(oilConsumed) * 0.01f;
        energy.oilDependency += growthRate;
    } else {
        energy.oilDependency -= 0.02f;
    }

    float renewableReduction  = energy.renewableOffset() * 0.01f;
    energy.oilDependency     -= renewableReduction;
    energy.oilDependency      = std::clamp(energy.oilDependency, 0.0f, 1.0f);
}

void updateGlobalOilReserves(const aoc::map::HexGrid& grid,
                              GlobalOilReserves& reserves) {
    int64_t totalOil = 0;

    for (int32_t i = 0; i < grid.tileCount(); ++i) {
        ResourceId res = grid.resource(i);
        if (res.isValid() && res.value == goods::OIL) {
            int16_t tileReserves = grid.reserves(i);
            if (tileReserves > 0) {
                totalOil += static_cast<int64_t>(tileReserves);
            }
        }
    }

    if (reserves.initialTotal == 0 && totalOil > 0) {
        reserves.initialTotal = totalOil;
    }

    reserves.totalRemaining = totalOil;

    if (!reserves.peakOilReached && reserves.initialTotal > 0) {
        float remaining = static_cast<float>(reserves.totalRemaining)
                        / static_cast<float>(reserves.initialTotal);
        if (remaining < 0.50f) {
            reserves.peakOilReached    = true;
            reserves.turnsSincePeakOil = 0;
            LOG_INFO("PEAK OIL reached! Global reserves at %.0f%% of initial",
                     static_cast<double>(remaining) * 100.0);
        }
    }

    if (reserves.peakOilReached) {
        ++reserves.turnsSincePeakOil;
    }
}

void processOilShock(PlayerEnergyComponent& energy) {
    if (energy.inOilShock) {
        --energy.oilShockTurnsRemaining;
        if (energy.oilShockTurnsRemaining <= 0) {
            energy.inOilShock = false;
            LOG_INFO("Player %u recovered from oil shock",
                     static_cast<unsigned>(energy.owner));
        }
    }
}

int32_t countRenewableBuildings(const aoc::game::GameState& gameState, PlayerId player) {
    const aoc::game::Player* playerObj = gameState.player(player);
    if (playerObj == nullptr) {
        return 0;
    }

    int32_t count = 0;
    for (const std::unique_ptr<aoc::game::City>& cityPtr : playerObj->cities()) {
        if (cityPtr == nullptr) { continue; }
        for (const CityDistrictsComponent::PlacedDistrict& district : cityPtr->districts().districts) {
            for (BuildingId bid : district.buildings) {
                // Renewable energy buildings: Hydroelectric(28), Nuclear(29), Solar(30), Wind(31)
                if (bid.value == 28 || bid.value == 29
                    || bid.value == 30 || bid.value == 31) {
                    ++count;
                }
            }
        }
    }

    return count;
}

} // namespace aoc::sim
