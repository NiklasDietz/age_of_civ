#pragma once

/**
 * @file CommandBuffer.hpp
 * @brief Deterministic command recording for multiplayer lockstep.
 *
 * Each turn, a player's actions are recorded as serializable commands.
 * In single-player, commands execute immediately. In multiplayer, commands
 * are serialized, sent to all peers, and executed in lockstep to ensure
 * identical simulation on all machines.
 */

#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"

#include <cstdint>
#include <variant>
#include <vector>

namespace aoc::net {

// ============================================================================
// Command types (all player actions that affect game state)
// ============================================================================

struct MoveUnitCommand {
    EntityId        unitEntity;
    hex::AxialCoord destination;
};

struct AttackUnitCommand {
    EntityId attacker;
    EntityId defender;
};

struct FoundCityCommand {
    EntityId        settlerEntity;
    std::string     cityName;
};

struct SetProductionCommand {
    EntityId cityEntity;
    uint16_t itemId;
    uint8_t  itemType;   ///< ProductionItemType cast
};

struct SetResearchCommand {
    PlayerId player;
    TechId   techId;
};

struct EndTurnCommand {
    PlayerId player;
};

struct SetTaxRateCommand {
    PlayerId   player;
    Percentage rate;
};

struct TransitionMonetaryCommand {
    PlayerId player;
    uint8_t  targetSystem;  ///< MonetarySystemType cast
};

/// A single player command (variant of all possible actions).
using GameCommand = std::variant<
    MoveUnitCommand,
    AttackUnitCommand,
    FoundCityCommand,
    SetProductionCommand,
    SetResearchCommand,
    EndTurnCommand,
    SetTaxRateCommand,
    TransitionMonetaryCommand
>;

/**
 * @brief Buffer of commands recorded during a player's turn.
 *
 * In lockstep multiplayer:
 *   1. Each player records commands into their buffer.
 *   2. At turn end, buffers are exchanged with all peers.
 *   3. All players execute all buffers in deterministic order.
 *   4. Game state should be identical on all machines.
 */
class CommandBuffer {
public:
    void push(GameCommand command) {
        this->m_commands.push_back(std::move(command));
    }

    void clear() { this->m_commands.clear(); }

    [[nodiscard]] const std::vector<GameCommand>& commands() const {
        return this->m_commands;
    }

    [[nodiscard]] bool empty() const { return this->m_commands.empty(); }
    [[nodiscard]] std::size_t size() const { return this->m_commands.size(); }

private:
    std::vector<GameCommand> m_commands;
};

} // namespace aoc::net
