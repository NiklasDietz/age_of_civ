#include "aoc/debug/GameControlValidation.hpp"

#include "aoc/simulation/city/District.hpp"  // BUILDING_DEFS, DISTRICT_TYPE_COUNT
#include "aoc/simulation/unit/UnitTypes.hpp" // UNIT_TYPE_COUNT
#include "aoc/simulation/wonder/Wonder.hpp"  // WONDER_COUNT

namespace aoc::debug {

bool isProductionItemValid(aoc::sim::ProductionItemType type, int32_t itemId) {
    switch (type) {
    case aoc::sim::ProductionItemType::Unit:
        return itemId < aoc::sim::UNIT_TYPE_COUNT;
    case aoc::sim::ProductionItemType::Building:
        return itemId < static_cast<int32_t>(aoc::sim::BUILDING_DEFS.size());
    case aoc::sim::ProductionItemType::Wonder:
        return itemId < static_cast<int32_t>(aoc::sim::WONDER_COUNT);
    case aoc::sim::ProductionItemType::District:
        return itemId < static_cast<int32_t>(aoc::sim::DISTRICT_TYPE_COUNT);
    default:
        return false;
    }
}

bool isResearchValid(const aoc::sim::PlayerTechComponent& tech, uint16_t techId) {
    if (techId >= aoc::sim::techCount()) {
        return false;
    }
    return tech.canResearch(aoc::TechId{techId});
}

} // namespace aoc::debug
