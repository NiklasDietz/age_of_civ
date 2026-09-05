#pragma once

#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/diplomacy/Espionage.hpp"
#include "aoc/simulation/diplomacy/WorldCongress.hpp"

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

/// Assign a spy mission to the spy unit standing at `at`.
struct AssignSpyMissionCommand {
    aoc::PlayerId player;
    aoc::hex::AxialCoord at;
    aoc::sim::SpyMission mission;
};

/// Activate the Great Person unit standing at `at`.
struct ActivateGreatPersonCommand {
    aoc::PlayerId player;
    aoc::hex::AxialCoord at;
};

/// Replace `player`'s vote on the open World Congress proposal.
struct CongressVoteCommand {
    aoc::PlayerId player;
    int32_t weight;
};

/// Register what `player` proposes the next time it is chosen as proposer.
struct CongressProposalCommand {
    aoc::PlayerId player;
    aoc::sim::Resolution resolution;
    aoc::PlayerId target;
};

using GameControlCommand = std::variant<MoveUnitCommand, AttackUnitCommand, FoundCityCommand,
                                        SetProductionCommand, SetResearchCommand,
                                        AssignSpyMissionCommand, ActivateGreatPersonCommand,
                                        CongressVoteCommand, CongressProposalCommand,
                                        EndTurnCommand>;

} // namespace aoc::debug
