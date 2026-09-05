#include "aoc/debug/GameControlValidation.hpp"

#include "aoc/simulation/city/District.hpp"  // BUILDING_DEFS, DISTRICT_TYPE_COUNT
#include "aoc/simulation/unit/UnitTypes.hpp" // UNIT_TYPE_COUNT
#include "aoc/simulation/wonder/Wonder.hpp"  // WONDER_COUNT

namespace aoc::debug {

bool isProductionItemValid(aoc::sim::ProductionItemType type, int32_t itemId) {
    switch (type) {
    case aoc::sim::ProductionItemType::Unit:
        // Ids are sparse (0..102 with gaps); a row must exist. The old
        // `itemId < UNIT_TYPE_COUNT` accepted the phantom 13 and rejected the Spy.
        return itemId >= 0 && itemId <= 255
            && aoc::sim::unitTypeDef(aoc::UnitTypeId{static_cast<uint16_t>(itemId)}).id.value
                   == static_cast<uint16_t>(itemId);
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
