#pragma once

/**
 * @file StateUpdate.hpp
 * @brief Real-time state delta messages broadcast to all clients.
 *
 * When a player performs an action (move unit, found city, declare war),
 * the server executes it immediately and broadcasts a StateUpdate to
 * ALL connected clients. This makes the game feel responsive -- other
 * players see actions as they happen, not after clicking end turn.
 *
 * Two message channels:
 *   1. StateUpdate (real-time): small delta after each player action.
 *      Broadcast immediately to all clients.
 *   2. TurnEndSnapshot (per-turn): full state update after simulation.
 *      Sent once per turn after all systems process.
 *
 * StateUpdate types match the command types 1:1, plus some server-generated
 * events (combat results, barbarian spawns, etc.)
 */

#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace aoc::net {

// ============================================================================
// State update types (server → all clients, real-time)
// ============================================================================

/// A unit moved to a new position.
struct UnitMovedUpdate {
    EntityId             unit;
    PlayerId             owner;
    aoc::hex::AxialCoord from;
    aoc::hex::AxialCoord to;
    int32_t              movementRemaining;
};

/// A unit was destroyed (killed, disbanded, consumed).
struct UnitDestroyedUpdate {
    EntityId             unit;
    PlayerId             owner;
    aoc::hex::AxialCoord position;
    uint8_t              reason;  ///< 0=combat, 1=disbanded, 2=consumed (settler), 3=attrition
};

/// Combat occurred between two units.
struct CombatResultUpdate {
    EntityId             attacker;
    EntityId             defender;
    PlayerId             attackerOwner;
    PlayerId             defenderOwner;
    int32_t              attackerHPAfter;
    int32_t              defenderHPAfter;
    bool                 attackerDestroyed;
    bool                 defenderDestroyed;
    aoc::hex::AxialCoord attackerPos;
    aoc::hex::AxialCoord defenderPos;
};

/// A new city was founded.
struct CityFoundedUpdate {
    EntityId             city;
    PlayerId             owner;
    std::string          name;
    aoc::hex::AxialCoord location;
};

/// A city changed owner (conquest, loyalty flip).
struct CityOwnerChangedUpdate {
    EntityId             city;
    PlayerId             previousOwner;
    PlayerId             newOwner;
    std::string          cityName;
};

/// A player declared war.
struct WarDeclaredUpdate {
    PlayerId aggressor;
    PlayerId target;
};

/// A peace treaty was signed.
struct PeaceDeclaredUpdate {
    PlayerId playerA;
    PlayerId playerB;
};

/// A player set their research target.
struct ResearchChangedUpdate {
    PlayerId    player;
    uint16_t    techId;
    std::string techName;
};

/// A player changed production in a city.
struct ProductionChangedUpdate {
    EntityId    city;
    PlayerId    owner;
    std::string itemName;
    float       totalCost;
};

/// A player completed a tech.
struct TechCompletedUpdate {
    PlayerId    player;
    uint16_t    techId;
    std::string techName;
};

/// A player completed a civic.
struct CivicCompletedUpdate {
    PlayerId    player;
    uint16_t    civicId;
    std::string civicName;
};

/// A unit was produced by a city.
struct UnitProducedUpdate {
    EntityId             unit;
    EntityId             city;
    PlayerId             owner;
    uint16_t             unitTypeId;
    aoc::hex::AxialCoord position;
};

/// A building was completed in a city.
struct BuildingCompletedUpdate {
    EntityId    city;
    PlayerId    owner;
    uint16_t    buildingId;
    std::string buildingName;
};

/// A wonder was completed.
struct WonderCompletedUpdate {
    EntityId    city;
    PlayerId    owner;
    uint8_t     wonderId;
    std::string wonderName;
};

/// Nuclear strike occurred.
struct NuclearStrikeUpdate {
    PlayerId             launcher;
    aoc::hex::AxialCoord target;
    uint8_t              type;  ///< 0=nuclear, 1=thermonuclear
    int32_t              blastRadius;
};

/// Diplomatic deal proposed or accepted.
struct DiplomaticDealUpdate {
    PlayerId playerA;
    PlayerId playerB;
    bool     accepted;
    std::string summary;
};

/// Chat message between players.
struct ChatMessageUpdate {
    PlayerId    sender;
    std::string message;
    bool        isGlobal;       ///< true = all players, false = specific recipient
    PlayerId    recipient;      ///< Only if !isGlobal
};

/// A player's turn is complete (visible to others as "waiting for X").
struct PlayerEndedTurnUpdate {
    PlayerId player;
};

/// Generic text notification for events.
struct NotificationUpdate {
    PlayerId    affectedPlayer;
    std::string title;
    std::string body;
    int32_t     priority;
};

// ============================================================================
// StateUpdate variant (one of the above)
// ============================================================================

using StateUpdate = std::variant<
    UnitMovedUpdate,
    UnitDestroyedUpdate,
    CombatResultUpdate,
    CityFoundedUpdate,
    CityOwnerChangedUpdate,
    WarDeclaredUpdate,
    PeaceDeclaredUpdate,
    ResearchChangedUpdate,
    ProductionChangedUpdate,
    TechCompletedUpdate,
    CivicCompletedUpdate,
    UnitProducedUpdate,
    BuildingCompletedUpdate,
    WonderCompletedUpdate,
    NuclearStrikeUpdate,
    DiplomaticDealUpdate,
    ChatMessageUpdate,
    PlayerEndedTurnUpdate,
    NotificationUpdate
>;

} // namespace aoc::net
