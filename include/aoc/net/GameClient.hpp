#pragma once

/**
 * @file GameClient.hpp
 * @brief Game client: sends commands, receives real-time updates + turn snapshots.
 *
 * The client receives two types of messages from the server:
 *   1. StateUpdate (real-time): immediate feedback when any player acts.
 *      Used to show unit movements, city founding, combat, etc. as they happen.
 *   2. GameStateSnapshot (per-turn): full state update after turn simulation.
 *      Used to update all yields, research progress, economy, etc.
 */

#include "aoc/net/Transport.hpp"
#include "aoc/net/StateUpdate.hpp"
#include "aoc/net/GameStateSnapshot.hpp"
#include "aoc/net/CommandBuffer.hpp"
#include "aoc/core/Types.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace aoc::net {

class GameClient {
public:
    GameClient() = default;

    /// Set the transport layer.
    void setTransport(ITransport* transport) { this->m_transport = transport; }

    /// Set this client's player ID.
    void setPlayer(PlayerId player) { this->m_localPlayer = player; }

    /// Get this client's player ID.
    [[nodiscard]] PlayerId player() const { return this->m_localPlayer; }

    // ========================================================================
    // Sending commands to server
    // ========================================================================

    void sendCommand(GameCommand command) {
        if (this->m_transport != nullptr) {
            this->m_transport->sendCommand(this->m_localPlayer, std::move(command));
        }
    }

    void endTurn() {
        this->sendCommand(EndTurnCommand{this->m_localPlayer});
    }

    void moveUnit(EntityId unit, aoc::hex::AxialCoord dest) {
        this->sendCommand(MoveUnitCommand{unit, dest});
    }

    void attackUnit(EntityId attacker, EntityId defender) {
        this->sendCommand(AttackUnitCommand{attacker, defender});
    }

    void foundCity(EntityId settler, const std::string& name) {
        this->sendCommand(FoundCityCommand{settler, name});
    }

    void setProduction(EntityId city, uint16_t itemId, uint8_t itemType) {
        this->sendCommand(SetProductionCommand{city, itemId, itemType});
    }

    void setResearch(TechId tech) {
        this->sendCommand(SetResearchCommand{this->m_localPlayer, tech});
    }

    void setTaxRate(Percentage rate) {
        this->sendCommand(SetTaxRateCommand{this->m_localPlayer, rate});
    }

    void sendChat(const std::string& message, bool global = true,
                  PlayerId recipient = INVALID_PLAYER) {
        this->sendCommand(GameCommand{});  // Chat would be a new command type
        (void)message; (void)global; (void)recipient;
    }

    // ========================================================================
    // Receiving real-time state updates (every frame)
    // ========================================================================

    /**
     * @brief Poll for real-time state updates.
     *
     * Call this every frame. Returns all updates received since last call.
     * The rendering system should process these to animate unit movements,
     * show combat results, display city founding, etc.
     */
    [[nodiscard]] std::vector<StateUpdate> pollUpdates() {
        if (this->m_transport == nullptr) {
            return {};
        }
        return this->m_transport->receivePendingUpdates();
    }

    // ========================================================================
    // Receiving turn-end snapshots (once per turn)
    // ========================================================================

    /**
     * @brief Poll for a turn-end snapshot.
     *
     * Returns true if a new snapshot was received (a turn was processed).
     * The full game state (economy, research, growth) is updated from this.
     */
    bool pollSnapshot() {
        if (this->m_transport == nullptr) {
            return false;
        }
        std::optional<GameStateSnapshot> snapshot =
            this->m_transport->receiveSnapshot(this->m_localPlayer);
        if (snapshot.has_value()) {
            this->m_latestSnapshot = std::move(*snapshot);
            this->m_hasNewSnapshot = true;
            return true;
        }
        return false;
    }

    [[nodiscard]] const GameStateSnapshot& latestSnapshot() const {
        return this->m_latestSnapshot;
    }

    [[nodiscard]] bool hasNewSnapshot() {
        bool result = this->m_hasNewSnapshot;
        this->m_hasNewSnapshot = false;
        return result;
    }

private:
    ITransport*       m_transport = nullptr;
    PlayerId          m_localPlayer = INVALID_PLAYER;
    GameStateSnapshot m_latestSnapshot{};
    bool              m_hasNewSnapshot = false;
};

} // namespace aoc::net
