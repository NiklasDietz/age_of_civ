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
 *
 * Threading contract (LocalTransport):
 *   - LocalTransport stores three unsynchronised vectors. It is NOT
 *     thread-safe. Every public method must be invoked from the same
 *     thread that constructed the instance (the "owner" thread).
 *   - The class records the owner thread id at construction. Every
 *     public method asserts the calling thread matches in debug builds
 *     (`#ifndef NDEBUG`); the check compiles to nothing in Release so
 *     there is no runtime cost.
 *   - Multiplayer over the network requires a different transport
 *     class with its own synchronisation policy; do not retrofit
 *     locks onto LocalTransport.
 */

#include "aoc/net/CommandBuffer.hpp"
#include "aoc/net/StateUpdate.hpp"
#include "aoc/core/Types.hpp"

#include <cstdint>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#ifndef NDEBUG
#  include <cassert>
#endif

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
 *
 * Single-thread contract: every public method must be called from the
 * same thread that constructed the instance. The constructor records
 * `std::this_thread::get_id()`; each public method asserts the caller
 * matches in debug builds. See file-header docs for the full contract.
 */
class LocalTransport final : public ITransport {
public:
    LocalTransport() = default;

    // Commands: client -> server
    void sendCommand(PlayerId player, GameCommand command) override {
        this->assertOwner();
        this->m_pendingCommands.push_back({player, std::move(command)});
    }

    [[nodiscard]] std::vector<std::pair<PlayerId, GameCommand>> receivePendingCommands() override {
        this->assertOwner();
        std::vector<std::pair<PlayerId, GameCommand>> result;
        result.swap(this->m_pendingCommands);
        return result;
    }

    // State updates: server -> client (real-time)
    void broadcastUpdate(const StateUpdate& update) override {
        this->assertOwner();
        this->m_pendingUpdates.push_back(update);
    }

    [[nodiscard]] std::vector<StateUpdate> receivePendingUpdates() override {
        this->assertOwner();
        std::vector<StateUpdate> result;
        result.swap(this->m_pendingUpdates);
        return result;
    }

    // Snapshots: server -> client (turn-end)
    void sendSnapshot(PlayerId player, GameStateSnapshot snapshot) override;

    [[nodiscard]] std::optional<GameStateSnapshot> receiveSnapshot(PlayerId player) override;

private:
    /// Debug-only ownership check. Compiles away in Release: no
    /// branch, no member load, no cost. The owner thread id is set
    /// at construction time so any later call from a different
    /// thread aborts immediately rather than silently corrupting
    /// the unsynchronised queues.
    void assertOwner() const noexcept {
#ifndef NDEBUG
        assert(std::this_thread::get_id() == this->m_owner
               && "LocalTransport called from a non-owner thread; "
                  "see Transport.hpp threading contract");
#endif
    }

    std::vector<std::pair<PlayerId, GameCommand>> m_pendingCommands;
    std::vector<StateUpdate> m_pendingUpdates;
    std::vector<std::pair<PlayerId, GameStateSnapshot>> m_pendingSnapshots;
#ifndef NDEBUG
    std::thread::id m_owner = std::this_thread::get_id();
#endif
};

} // namespace aoc::net
