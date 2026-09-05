#pragma once

#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/diplomacy/Espionage.hpp"
#include "aoc/simulation/diplomacy/WorldCongress.hpp"

#include <cstdint>
#include <string>
#include <variant>
#include "aoc/simulation/city/Governor.hpp"
#include "aoc/simulation/government/Government.hpp"
#include "aoc/simulation/city/DistrictAdjacency.hpp"
#include "aoc/map/HexGrid.hpp"

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

/// Seat a named governor in the city at `at` (a title recruits a new one; moving is free).
struct AssignGovernorCommand {
    aoc::PlayerId player;
    aoc::hex::AxialCoord at;
    aoc::sim::GovernorType type;
};

/// Buy a title for the governor seated in the city at `at`.
struct PromoteGovernorCommand {
    aoc::PlayerId player;
    aoc::hex::AxialCoord at;
    aoc::sim::GovernorPromotion promotion;
};

/// Put policy card `policy` (-1 clears) into policy slot `slot`.
struct SlotPolicyCommand {
    aoc::PlayerId player;
    uint8_t slot;
    int8_t policy;
};

/// Adopt an unlocked government (anarchy unless leaving Chiefdom; cooldown applies).
struct ChangeGovernmentCommand {
    aoc::PlayerId player;
    aoc::sim::GovernmentType government;
};

/// Buy a unit or building in the city at `at` (gold), or a religious unit with faith.
struct CityPurchaseCommand {
    aoc::PlayerId player;
    aoc::hex::AxialCoord at;
    aoc::sim::ProductionItemType type;
    uint16_t itemId;
    bool withFaith;
};

/// Set the citizen focus of the city at `at`.
struct SetCityFocusCommand {
    aoc::PlayerId player;
    aoc::hex::AxialCoord at;
    aoc::sim::CityFocus focus;
};

/// Pin or unpin `tile` for the city at `at`.
struct ToggleTileLockCommand {
    aoc::PlayerId player;
    aoc::hex::AxialCoord at;
    aoc::hex::AxialCoord tile;
};

/// Drop entry `index` from the queue of the city at `at`.
struct RemoveQueueItemCommand {
    aoc::PlayerId player;
    aoc::hex::AxialCoord at;
    int32_t index;
};

/// Queue a city project in the city at `at`.
struct QueueProjectCommand {
    aoc::PlayerId player;
    aoc::hex::AxialCoord at;
    aoc::sim::CityProjectType project;
};

/// Place improvement `type` with the civilian unit standing on `at`.
struct PlaceImprovementCommand {
    aoc::PlayerId player;
    aoc::hex::AxialCoord at;
    aoc::map::ImprovementType type;
};

/// Chop the feature under the Builder on `at`.
struct BuilderChopCommand {
    aoc::PlayerId player;
    aoc::hex::AxialCoord at;
};

/// Harvest the bonus resource under the Builder on `at`.
struct BuilderHarvestCommand {
    aoc::PlayerId player;
    aoc::hex::AxialCoord at;
};

/// Pillage the improvement under the military unit on `at`.
struct PillageCommand {
    aoc::PlayerId player;
    aoc::hex::AxialCoord at;
};

/// Repair the pillaged tile under the Builder on `at`.
struct RepairCommand {
    aoc::PlayerId player;
    aoc::hex::AxialCoord at;
};

/// Disband the unit on `at`.
struct DeleteUnitCommand {
    aoc::PlayerId player;
    aoc::hex::AxialCoord at;
};

/// Set or clear the alert stance of the unit on `at`.
struct SetAlertCommand {
    aoc::PlayerId player;
    aoc::hex::AxialCoord at;
    bool alert;
};

/// Give the unit on `at` the promotion it has earned.
struct PromoteUnitCommand {
    aoc::PlayerId player;
    aoc::hex::AxialCoord at;
    aoc::PromotionId promotion;
};

/// Found a pantheon with a chosen follower belief.
struct FoundPantheonCommand {
    aoc::PlayerId player;
    uint8_t belief;
};

/// Found a religion with chosen founder / worship / enhancer beliefs.
struct FoundReligionCommand {
    aoc::PlayerId player;
    uint8_t founder;
    uint8_t worship;
    uint8_t enhancer;
};

/// Move the Great Work at `index` from the player's city at `from` into a free
/// slot of the player's city at `to`.
struct MoveGreatWorkCommand {
    aoc::PlayerId player;
    aoc::hex::AxialCoord from;
    int32_t index;
    aoc::hex::AxialCoord to;
};

/// Merge the unit at `sourceAt` into the same-type unit at `at` (Corps / Army, Fleet / Armada).
struct MergeUnitsCommand {
    aoc::PlayerId player;
    aoc::hex::AxialCoord at;
    aoc::hex::AxialCoord sourceAt;
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
                                        MergeUnitsCommand, AssignGovernorCommand,
                                        PromoteGovernorCommand, SlotPolicyCommand,
                                        ChangeGovernmentCommand, CityPurchaseCommand,
                                        SetCityFocusCommand, ToggleTileLockCommand,
                                        RemoveQueueItemCommand, QueueProjectCommand,
                                        PlaceImprovementCommand, BuilderChopCommand,
                                        BuilderHarvestCommand, PillageCommand, RepairCommand,
                                        DeleteUnitCommand, SetAlertCommand, PromoteUnitCommand,
                                        FoundPantheonCommand, FoundReligionCommand,
                                        MoveGreatWorkCommand, EndTurnCommand>;

} // namespace aoc::debug
