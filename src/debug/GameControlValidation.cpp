#include "aoc/debug/GameControlValidation.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/city/DistrictAdjacency.hpp"

#include "aoc/simulation/city/District.hpp"          // BUILDING_DEFS, DISTRICT_TYPE_COUNT
#include "aoc/simulation/diplomacy/DealTerms.hpp"    // SUPPLY_CONTRACT_MAX_TURNS
#include "aoc/simulation/resource/ResourceTypes.hpp" // GOOD_COUNT
#include "aoc/simulation/unit/UnitTypes.hpp"         // UNIT_TYPE_COUNT
#include "aoc/simulation/wonder/Wonder.hpp"          // WONDER_COUNT

namespace aoc::debug {

bool isProductionItemValid(aoc::sim::ProductionItemType type, int32_t itemId) {
    switch (type) {
    case aoc::sim::ProductionItemType::Unit:
        // Ids are sparse (0..102 with gaps); a row must exist. The old
        // `itemId < UNIT_TYPE_COUNT` accepted the phantom 13 and rejected the Spy.
        return itemId >= 0 && itemId <= 255 &&
               aoc::sim::unitTypeDef(aoc::UnitTypeId{static_cast<uint16_t>(itemId)}).id.value ==
                   static_cast<uint16_t>(itemId);
    case aoc::sim::ProductionItemType::Building:
        return itemId < static_cast<int32_t>(aoc::sim::BUILDING_DEFS.size());
    case aoc::sim::ProductionItemType::Wonder:
        return itemId < static_cast<int32_t>(aoc::sim::WONDER_COUNT);
    case aoc::sim::ProductionItemType::District:
        return itemId < static_cast<int32_t>(aoc::sim::DISTRICT_TYPE_COUNT);
    case aoc::sim::ProductionItemType::Project:
        return itemId >= 0 && itemId < static_cast<int32_t>(aoc::sim::CityProjectType::Count);
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

namespace {

/// -1 means "no such leg"; anything else must name a good.
[[nodiscard]] bool goodLegValid(int32_t goodId) {
    return goodId == -1 || (goodId >= 0 && goodId < aoc::sim::goods::GOOD_COUNT);
}

} // namespace

std::string_view dealCommandError(const ProposeDealCommand& cmd) {
    if (cmd.player == cmd.target) {
        return "player and target must differ";
    }
    if (cmd.giveGold < 0 || cmd.askGold < 0) {
        return "gold must not be negative";
    }
    if (!goodLegValid(cmd.goodId) || !goodLegValid(cmd.contractGood) ||
        !goodLegValid(cmd.exclusiveGood)) {
        return "good id out of range";
    }
    if (cmd.goodId >= 0 && cmd.goodAmount <= 0) {
        return "goodAmount must be positive";
    }
    if (cmd.contractGood >= 0) {
        if (cmd.contractPerTurn <= 0) {
            return "contractPerTurn must be positive";
        }
        if (cmd.contractGold < 0) {
            return "contractGold must not be negative";
        }
        if (cmd.contractTurns <= 0 || cmd.contractTurns > aoc::sim::SUPPLY_CONTRACT_MAX_TURNS) {
            return "contractTurns out of range";
        }
    }
    return {};
}

std::string_view moneyGoodCommandError(const MoneyGoodCommand& cmd) {
    if (cmd.player >= MAX_PLAYERS) {
        return "player out of range";
    }
    // 0xFF is the demonetise sentinel, not a good; everything else must name a
    // row in the table. Whether the civ may actually elect it is
    // requestSetMoneyGood's call, not this one's.
    if (cmd.goodId != aoc::sim::NO_MONEY_GOOD && cmd.goodId >= aoc::sim::goods::GOOD_COUNT) {
        return "good id out of range";
    }
    return {};
}

std::string_view regimeCommandError(const MonetaryRegimeCommand& cmd) {
    if (cmd.player >= MAX_PLAYERS) {
        return "player out of range";
    }
    if (cmd.target == 0 ||
        cmd.target >= static_cast<uint8_t>(aoc::sim::MonetarySystemType::Count)) {
        return "target must be a monetary system above Barter";
    }
    return {};
}

} // namespace aoc::debug
