/**
 * @file GreatWorks.cpp
 * @brief Great Work placement and tallies. See GreatWorks.hpp.
 */

#include "aoc/simulation/culture/GreatWorks.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/simulation/city/District.hpp"

#include <limits>
#include <memory>

namespace aoc::sim {

int32_t greatWorkCapacity(const aoc::game::City& city) {
    int32_t total = 0;
    for (const CityDistrictsComponent::PlacedDistrict& district : city.districts().districts) {
        for (const BuildingId b : district.buildings) {
            if (b.value < BUILDING_DEFS.size()) {
                total += static_cast<int32_t>(buildingDef(b).greatWorksSlots);
            }
        }
    }
    return total;
}

int32_t freeGreatWorkSlots(const aoc::game::City& city) {
    const int32_t used = static_cast<int32_t>(city.greatWorks().works.size());
    const int32_t free = greatWorkCapacity(city) - used;
    return free > 0 ? free : 0;
}

bool placeGreatWork(aoc::game::City& city, const GreatWork& work) {
    if (freeGreatWorkSlots(city) <= 0) {
        return false;
    }
    city.greatWorks().works.push_back(work);
    return true;
}

aoc::game::City* cityWithFreeGreatWorkSlot(aoc::game::Player& owner, const aoc::map::HexGrid& grid,
                                           hex::AxialCoord near) {
    aoc::game::City* best = nullptr;
    int32_t bestDistance  = std::numeric_limits<int32_t>::max();
    for (const std::unique_ptr<aoc::game::City>& city : owner.cities()) {
        if (city == nullptr || freeGreatWorkSlots(*city) <= 0) {
            continue;
        }
        const int32_t distance = grid.distance(near, city->location());
        if (distance < bestDistance) {
            bestDistance = distance;
            best         = city.get();
        }
    }
    return best;
}

GreatWorkTally tallyGreatWorks(const aoc::game::Player& owner) {
    GreatWorkTally tally;
    for (const std::unique_ptr<aoc::game::City>& city : owner.cities()) {
        if (city == nullptr) {
            continue;
        }
        tally.works += static_cast<int32_t>(city->greatWorks().works.size());
        tally.capacity += greatWorkCapacity(*city);
    }
    return tally;
}

} // namespace aoc::sim
