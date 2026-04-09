#pragma once

/**
 * @file Transport.hpp
 * @brief Transport layer interface for server/client communication.
 *
 * Two message channels:
 *   1. StateUpdate (real-time): broadcast to all clients immediately after
 *      each player action. Small delta messages.
 *   2. TurnEndSnapshot (per-turn): full state update sent after the
 *      simulation tick at end of turn.
 *
 * LocalTransport: in-process queues (single player, zero overhead).
 * NetworkTransport: TCP sockets (multiplayer, future).
 */

#include "aoc/net/CommandBuffer.hpp"
#include "aoc/net/StateUpdate.hpp"
#include "aoc/core/Types.hpp"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace aoc::net {

struct GameStateSnapshot;

/**
 * @brief Abstract transport interface.
 */
class ITransport {
public:
    virtual ~ITransport() = default;

    // -- Client -> Server: commands --

    /// Send a player command to the server.
    virtual void sendCommand(PlayerId player, GameCommand command) = 0;

    /// Server: get all commands received since last call.
    [[nodiscard]] virtual std::vector<std::pair<PlayerId, GameCommand>> receivePendingCommands() = 0;

    // -- Server -> All Clients: real-time state updates --

    /// Broadcast a state update to ALL clients (after an action executes).
    virtual void broadcastUpdate(const StateUpdate& update) = 0;

    /// Client: get all state updates received since last call.
    [[nodiscard]] virtual std::vector<StateUpdate> receivePendingUpdates() = 0;

    // -- Server -> Specific Client: turn-end snapshot --

    /// Send end-of-turn snapshot to a specific player.
    virtual void sendSnapshot(PlayerId player, GameStateSnapshot snapshot) = 0;

    /// Client: get latest turn-end snapshot (if available).
    [[nodiscard]] virtual std::optional<GameStateSnapshot> receiveSnapshot(PlayerId player) = 0;
};

/**
 * @brief In-process transport for single player. Zero overhead.
 */
class LocalTransport final : public ITransport {
public:
    // Commands: client -> server
    void sendCommand(PlayerId player, GameCommand command) override {
        this->m_pendingCommands.push_back({player, std::move(command)});
    }

    [[nodiscard]] std::vector<std::pair<PlayerId, GameCommand>> receivePendingCommands() override {
        std::vector<std::pair<PlayerId, GameCommand>> result;
        result.swap(this->m_pendingCommands);
        return result;
    }

    // State updates: server -> client (real-time)
    void broadcastUpdate(const StateUpdate& update) override {
        this->m_pendingUpdates.push_back(update);
    }

    [[nodiscard]] std::vector<StateUpdate> receivePendingUpdates() override {
        std::vector<StateUpdate> result;
        result.swap(this->m_pendingUpdates);
        return result;
    }

    // Snapshots: server -> client (turn-end)
    void sendSnapshot(PlayerId player, GameStateSnapshot snapshot) override;

    [[nodiscard]] std::optional<GameStateSnapshot> receiveSnapshot(PlayerId player) override;

private:
    std::vector<std::pair<PlayerId, GameCommand>> m_pendingCommands;
    std::vector<StateUpdate> m_pendingUpdates;
    std::vector<std::pair<PlayerId, GameStateSnapshot>> m_pendingSnapshots;
};

} // namespace aoc::net
