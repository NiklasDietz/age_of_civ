#pragma once

/**
 * @file DebugConsole.hpp
 * @brief In-game debug console for testing and cheats.
 *
 * Open with ` (backtick/tilde) key. Type commands and press Enter.
 *
 * Commands:
 *   reveal          - Reveal entire map (disable fog of war)
 *   fog             - Re-enable fog of war
 *   gold <amount>   - Add gold to human player treasury
 *   tech <id>       - Research a tech instantly
 *   techall         - Research all techs
 *   advance <turns> - Skip forward N turns
 *   spawn <unitId>  - Spawn a unit at camera center
 *   resource <id> <amount> - Add resource to nearest city stockpile
 *   kill <playerId> - Eliminate a player
 *   era <name>      - Jump to a specific era
 *   money <system>  - Set monetary system (barter/commodity/gold/fiat)
 *   pop <amount>    - Set nearest city population
 *   loyalty <value> - Set nearest city loyalty
 *   help            - List all commands
 */

#include "aoc/core/Types.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace aoc::game { class GameState; }
namespace aoc::map { class HexGrid; class FogOfWar; }

namespace aoc::ui {

class DebugConsole {
public:
    DebugConsole() = default;

    /// Whether the console is currently visible/active.
    [[nodiscard]] bool isOpen() const { return this->m_isOpen; }

    /// Toggle console visibility.
    void toggle() { this->m_isOpen = !this->m_isOpen; }

    /// Add a character to the input buffer.
    void addChar(char c);

    /// Handle backspace.
    void backspace();

    /// Execute the current input and clear it.
    void execute(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                 aoc::map::FogOfWar& fog, PlayerId humanPlayer);

    /// Get current input string (for rendering).
    [[nodiscard]] const std::string& input() const { return this->m_input; }

    /// Get output history (for rendering).
    [[nodiscard]] const std::vector<std::string>& history() const { return this->m_history; }

    /// Callback for advancing turns (set by Application).
    std::function<void(int32_t)> onAdvanceTurns;

private:
    void log(const std::string& msg);

    bool m_isOpen = false;
    std::string m_input;
    std::vector<std::string> m_history;
};

} // namespace aoc::ui
