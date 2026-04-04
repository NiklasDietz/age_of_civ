#pragma once

/**
 * @file NetInterface.hpp
 * @brief Multiplayer network interface stub.
 *
 * Defines the abstract interface for sending/receiving command buffers
 * between peers. The default implementation (LocalOnly) executes everything
 * locally -- it's what single-player uses.
 *
 * Future implementations: LAN (UDP broadcast), Online (relay server).
 */

#include "aoc/net/CommandBuffer.hpp"
#include "aoc/core/Types.hpp"

#include <cstdint>
#include <optional>

namespace aoc::net {

enum class NetworkMode : uint8_t {
    LocalOnly,   ///< Single-player, no networking
    LAN,         ///< Local area network (future)
    Online,      ///< Internet relay server (future)
};

class NetInterface {
public:
    virtual ~NetInterface() = default;

    [[nodiscard]] virtual NetworkMode mode() const = 0;

    /// Send this player's commands for the given turn to all peers.
    virtual void sendCommands(TurnNumber turn, PlayerId player,
                              const CommandBuffer& commands) = 0;

    /// Receive another player's commands for the given turn.
    /// Returns nullopt if not yet received (async) or not applicable (local).
    [[nodiscard]] virtual std::optional<CommandBuffer> receiveCommands(
        TurnNumber turn, PlayerId player) = 0;

    /// Check if all players' commands have been received for this turn.
    [[nodiscard]] virtual bool allCommandsReceived(TurnNumber turn) const = 0;
};

/// Single-player implementation: commands execute locally, no networking.
class LocalOnlyNetwork final : public NetInterface {
public:
    [[nodiscard]] NetworkMode mode() const override { return NetworkMode::LocalOnly; }

    void sendCommands(TurnNumber /*turn*/, PlayerId /*player*/,
                      const CommandBuffer& /*commands*/) override {
        // No-op in single player
    }

    [[nodiscard]] std::optional<CommandBuffer> receiveCommands(
        TurnNumber /*turn*/, PlayerId /*player*/) override {
        return std::nullopt;  // All commands are local
    }

    [[nodiscard]] bool allCommandsReceived(TurnNumber /*turn*/) const override {
        return true;  // Always ready in single player
    }
};

} // namespace aoc::net
