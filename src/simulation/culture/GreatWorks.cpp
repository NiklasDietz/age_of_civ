/**
 * @file GreatWorks.cpp
 * @brief Great Work placement and tallies. See GreatWorks.hpp.
 */

#include "aoc/simulation/culture/GreatWorks.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/greatpeople/GreatPeopleExpanded.hpp"

#include <limits>
#include <memory>
#include <string>

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

std::string describeGreatWork(const GreatWork& work) {
    std::string text(greatWorkTypeName(work.type));
    if (static_cast<int32_t>(work.namedId) < NAMED_GP_COUNT) {
        text += " by ";
        text += namedGreatPersonDef(work.namedId).name;
    }
    text += " (turn " + std::to_string(work.createdTurn) + ")";
    return text;
}

ErrorCode requestMoveGreatWork(aoc::game::GameState& gameState, PlayerId player,
                               hex::AxialCoord fromCity, int32_t index, hex::AxialCoord toCity) {
    aoc::game::Player* owner = gameState.player(player);
    if (owner == nullptr) {
        return ErrorCode::EntityNotFound;
    }
    aoc::game::City* from = owner->cityAt(fromCity);
    aoc::game::City* to   = owner->cityAt(toCity);
    if (from == nullptr || to == nullptr || from->owner() != player || to->owner() != player) {
        return ErrorCode::EntityNotFound;
    }
    if (from == to) {
        return ErrorCode::InvalidArgument;
    }
    std::vector<GreatWork>& works = from->greatWorks().works;
    if (index < 0 || index >= static_cast<int32_t>(works.size())) {
        return ErrorCode::InvalidArgument;
    }
    if (freeGreatWorkSlots(*to) <= 0) {
        return ErrorCode::InvalidCityAction;
    }
    const GreatWork work = works[static_cast<size_t>(index)];
    works.erase(works.begin() + index);
    to->greatWorks().works.push_back(work);
    return ErrorCode::Ok;
}

} // namespace aoc::sim
