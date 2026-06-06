#pragma once

/**
 * @file GameStateSnapshot.hpp
 * @brief Per-player view of game state, sent from server to client each turn.
 *
 * Contains everything the client needs to render the game for one player.
 *
 * Visibility contract: the snapshot must contain only what the requesting
 * player is allowed to see. The intended end state is a per-player
 * fog-of-war filter (own entities always; foreign entities only on
 * currently-visible tiles). DEBT(net): the server has no FogOfWar instance
 * wired in yet, so GameServer::generateSnapshot currently ships the
 * requesting player's OWN units and cities only -- the safe subset. The
 * `units`/`cities` "own + visible foreign" comments below describe the
 * target behaviour, not today's; foreign entities are omitted until the
 * fog query lands. See src/net/GameServer.cpp generateSnapshot().
 */

#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace aoc::net {

/// Visible unit data (what the client sees).
struct VisibleUnit {
    EntityId            entity;
    PlayerId            owner;
    uint16_t            unitTypeId;
    aoc::hex::AxialCoord position;
    int32_t             hitPoints;
    int32_t             maxHitPoints;
    int32_t             movementRemaining;
};

/// Visible city data.
struct VisibleCity {
    EntityId            entity;
    PlayerId            owner;
    std::string         name;
    aoc::hex::AxialCoord location;
    int32_t             population;
    bool                isCapital;
    // Additional data only for the owning player:
    float               foodSurplus;
    float               productionPerTurn;
    float               sciencePerTurn;
    std::string         currentProduction;
    float               productionProgress;
    float               productionCost;
    float               happiness;
    float               loyalty;
};

/// Notification that happened this turn.
struct TurnNotification {
    std::string title;
    std::string body;
    int32_t     priority;
};

/// Economy summary for the player.
struct EconomySummary {
    CurrencyAmount gdp;
    CurrencyAmount treasury;
    uint8_t        monetarySystem;
    uint8_t        coinTier;
    float          inflationRate;
    uint8_t        governmentType;
    uint8_t        industrialRevolution;
    int32_t        eraVictoryPoints;
    float          compositeCSI;
};

/// Per-player snapshot of the game state.
struct GameStateSnapshot {
    PlayerId                      forPlayer;
    TurnNumber                    turnNumber;

    /// Economy summary
    EconomySummary                economy;

    /// Visible units (own + visible foreign)
    std::vector<VisibleUnit>      units;

    /// Visible cities (own + visible foreign)
    std::vector<VisibleCity>      cities;

    /// Notifications generated this turn
    std::vector<TurnNotification> notifications;

    /// Whether the game is over
    bool                          gameOver;
    uint8_t                       victoryType;
    PlayerId                      winner;

    /// Current research (own player only)
    std::string                   currentResearchName;
    float                         researchProgress;
    float                         researchCost;

    /// Current civic research
    std::string                   currentCivicName;
    float                         civicProgress;
    float                         civicCost;

    /// Tech/civic completion flags (for UI updates)
    bool                          techCompletedThisTurn;
    bool                          civicCompletedThisTurn;
    std::string                   completedTechName;
    std::string                   completedCivicName;
};

} // namespace aoc::net
