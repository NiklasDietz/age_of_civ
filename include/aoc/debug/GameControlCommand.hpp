#pragma once

#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"

#include <cstdint>
#include <string>
#include <variant>

namespace aoc::debug {

/// Move a unit toward a target tile (may take multiple turns if the path
/// exceeds remaining movement points, matching the existing UI behaviour).
struct MoveUnitCommand {
    aoc::PlayerId player;
    aoc::hex::AxialCoord from;
    aoc::hex::AxialCoord to;
};

/// Attack the unit occupying the target tile. Melee vs ranged is chosen
/// automatically from the attacker's `rangedStrength()`.
struct AttackUnitCommand {
    aoc::PlayerId player;
    aoc::hex::AxialCoord from;
    aoc::hex::AxialCoord to;
};

/// Found a city with a Settler unit at its current position.
struct FoundCityCommand {
    aoc::PlayerId player;
    aoc::hex::AxialCoord at;
    std::string name;
};

/// Enqueue a production item onto a city's build queue. `itemId` is
/// interpreted per `type` (UnitTypeId / BuildingId / DistrictType / WonderId).
struct SetProductionCommand {
    aoc::PlayerId player;
    aoc::hex::AxialCoord cityLocation;
    aoc::sim::ProductionItemType type;
    uint16_t itemId;
};

/// Start researching a tech.
struct SetResearchCommand {
    aoc::PlayerId player;
    uint16_t techId;
};

/// End the current turn. Runs last within a drain pass, after any
/// moves/attacks/production/research queued in the same frame.
struct EndTurnCommand {};

using GameControlCommand = std::variant<MoveUnitCommand, AttackUnitCommand, FoundCityCommand,
                                        SetProductionCommand, SetResearchCommand, EndTurnCommand>;

} // namespace aoc::debug
