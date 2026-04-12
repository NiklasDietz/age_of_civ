#pragma once

/**
 * @file FogOfWar.hpp
 * @brief Per-player tile visibility tracking.
 *
 * Three visibility states per tile per player:
 *   0 = Unseen (never explored, not rendered)
 *   1 = Revealed (explored but no current vision, rendered dimmed)
 *   2 = Visible (currently in a unit/city's sight range, fully rendered)
 */

#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"

#include <cstdint>
#include <vector>

namespace aoc::game {
class GameState;
}

namespace aoc::map {

class HexGrid;

enum class TileVisibility : uint8_t {
    Unseen   = 0,
    Revealed = 1,
    Visible  = 2,
};

class FogOfWar {
public:
    /**
     * @brief Initialize fog for a given map size and player count.
     *
     * All tiles start as Unseen for all players.
     */
    void initialize(int32_t tileCount, uint8_t playerCount);

    /**
     * @brief Recalculate visibility for a specific player.
     *
     * Sets all tiles to Revealed (previously visible) or Unseen,
     * then marks tiles within sight range of the player's units and cities
     * as Visible.
     *
     * @param world  ECS world with unit and city components.
     * @param grid   Hex grid for coordinate conversion.
     * @param player The player whose visibility to update.
     */
    void updateVisibility(const aoc::game::GameState& gameState,
                          const HexGrid& grid,
                          PlayerId player);

    /**
     * @brief Get visibility of a tile for a player.
     */
    [[nodiscard]] TileVisibility visibility(PlayerId player, int32_t tileIndex) const;

    /// Override visibility for a tile (used by debug console 'reveal' command).
    void setVisibility(PlayerId player, int32_t tileIndex, TileVisibility vis);

    /// Default sight range for units.
    static constexpr int32_t DEFAULT_SIGHT_RANGE = 2;
    /// Extended sight range for scouts.
    static constexpr int32_t SCOUT_SIGHT_RANGE   = 3;
    /// Sight range for cities.
    static constexpr int32_t CITY_SIGHT_RANGE    = 3;

private:
    /// Flat array: [player * tileCount + tileIndex] -> TileVisibility.
    std::vector<TileVisibility> m_visibility;
    int32_t m_tileCount   = 0;
    uint8_t m_playerCount = 0;
};

} // namespace aoc::map
